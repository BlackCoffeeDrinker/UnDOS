#include "stdkrn.hpp"

#include "CommandLineParser.hpp"
#include "configuration_manager.hpp"
#include "crt_init.hpp"
#include "dispatch.hpp"
#include "driver_loader.hpp"
#include "event_broadcaster.hpp"
#include "gfx/gfx.hpp"
#include "initial/boot_device_vfs.hpp"
#include "initial/root_volume_selector.hpp"
#include "input/input_manager.hpp"
#include "module/module_registry.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"
#include "profiler/profiler.hpp"
#include "resource_manager.hpp"
#include "scheduler/CpuContext.hpp"
#include "scheduler/Scheduler.hpp"
#include "smp.hpp"
#include "subsystem_loader.hpp"
#include "tty/tty_pump.hpp"
#include "user/user.hpp"
#include "vfs.hpp"
#include "vmm.hpp"

uint32_t g_page_size;

namespace {

const char *MappedRegionBaseName(kernel::MappedMemoryRegionType type) {
  switch (type) {
    case kernel::MappedMemoryRegionType::Identity: return "Identity";
    case kernel::MappedMemoryRegionType::KernelCore: return "KernelCore";
    case kernel::MappedMemoryRegionType::HalModule: return "HalModule";
    case kernel::MappedMemoryRegionType::BootStack: return "BootStack";
    case kernel::MappedMemoryRegionType::BootInfo: return "BootInfo";
    case kernel::MappedMemoryRegionType::PagingStructures: return "PagingStructures";
    case kernel::MappedMemoryRegionType::None: break;
  }
  return "Unknown";
}

// Publishes Stage 1.5's mapped regions under \Memory so they remain
// inspectable (and the BootInfo region reclaimable) once BootInfoT is gone.
void RegisterBootMappedRegions(const kernel::BootInfoT &boot_info) {
  const auto memoryDirectory = KE_OB_GetMemoryDirectory();
  size_t pagingIndex = 0;
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type == kernel::MappedMemoryRegionType::None) continue;

    kstd::static_string<64> name;
    if (region.type == kernel::MappedMemoryRegionType::PagingStructures) {
      kstd::format(name, "PagingStructures{}", pagingIndex++);
    } else {
      name = MappedRegionBaseName(region.type);
    }

    auto object = kernel::CreateKObject<kernel::KMappedRegionObject>(name);
    if (!object) {
      KE_Print("Failed to create mapped region object {}: {}", name, object.error().message());
      HAL_PLATFORM_Panic("Failed to create mapped region object", __FILE__, __LINE__);
      continue;
    }
    object.value()->regionType = region.type;
    object.value()->physicalBase = region.physical_base;
    object.value()->virtualBase = region.virtual_base;
    object.value()->length = region.length;
    if (const auto status = KE_OB_InsertObject(memoryDirectory, *object)) {
      KE_Print("Failed to register mapped region {}: {}", name, status.message());
      HAL_PLATFORM_Panic("Failed to register mapped region", __FILE__, __LINE__);
    }
  }
}

// Releases the Stage 1.5 BootInfo pages back to the PMM. Everything still needed
// (symbols, command line) must already be copied out; the BootInfoT passed to
// kernel_core_main and any view into it are invalid afterwards.
void MemoryReclaim_BootStructures() {
  const auto memoryDirectory = KE_OB_GetMemoryDirectory();
  const auto region = KE_OB_FindDirectChildOfType<kernel::KMappedRegionObject>(memoryDirectory, "BootInfo");
  if (!region) return;

  const size_t pages = (region->length + g_page_size - 1) / g_page_size;
  const auto physicalBase = region->physicalBase;
  const auto virtualBase = region->virtualBase;

  if (const auto status = KE_OB_RemoveObject(memoryDirectory, region)) {
    KE_Print("Failed to remove BootInfo region object: {}", status.message());
    HAL_PLATFORM_Panic("Failed to remove BootInfo region object", __FILE__, __LINE__);
  }

  for (size_t i = 0; i < pages; ++i) {
    const auto page = virtualBase + i * g_page_size;
    HAL_VMM_UnmapPage(page);
    HAL_VMM_Flush(page);
  }
  HAL_PMM_FreeFrames(physicalBase, pages);
  KE_Print("Reclaimed {} boot info pages", pages);
}

const char *GetObjectTypeName(kernel::ObjectType type) {
  if (type == kernel::TYPE_DIRECTORY) return "Directory";
  if (type == kernel::TYPE_DEVICE) return "Device";
  if (type == kernel::TYPE_DRIVER) return "Driver";
  if (type == kernel::TYPE_VMM) return "VMM";
  if (type == kernel::TYPE_MODULE) return "Module";
  if (type == kernel::TYPE_THREAD) return "Thread";
  return "Unknown";
}

kstd::string_view Indent(int depth) noexcept {
  static constexpr char kSpaces[] = "                                                                ";
  constexpr size_t kMax = sizeof(kSpaces) - 1;
  const size_t n = static_cast<size_t>(depth < 0 ? 0 : depth) * 2;
  return {kSpaces, n < kMax ? n : kMax};
}

[[maybe_unused]] void DumpObjectTree(const kernel::KObjectPtr<kernel::KObject> &obj, int depth = 0) {
  if (!obj) return;

  kstd::string_view name = obj->name;
  if (name.empty() && depth == 0) {
    name = "/";
  }

  KE_Print("{}|-- {} ({})", Indent(depth), name, GetObjectTypeName(obj->type));

  if (const auto directory = obj.As<kernel::KDirectoryObject>()) {
    for (const auto entry: directory->child_list) {
      DumpObjectTree(entry, depth + 1);
    }
  }
}

const char *GetDeviceTypeName(kernel::DeviceType type) {
  if (type == kernel::device_type::Bus) return "Bus";
  if (type == kernel::device_type::DiskController) return "DiskController";
  if (type == kernel::device_type::Disk) return "Disk";
  if (type == kernel::device_type::Optical) return "Optical";
  if (type == kernel::device_type::Keyboard) return "Keyboard";
  if (type == kernel::device_type::Mouse) return "Mouse";
  if (type == kernel::device_type::Video) return "Video";
  if (type == kernel::device_type::Serial) return "Serial";
  if (type == kernel::device_type::Parallel) return "Parallel";
  if (type == kernel::device_type::Volume) return "Volume";
  return "Unknown";
}

// Walks the device stack / devnode tree so that unnamed PDOs/FDOs (which never
// enter the \Device namespace) are still visible for debugging. For each
// device it follows the attached (upper) device and, for bus devices, recurses
// into the driver-reported children.
void DumpDevice(const kernel::KDevicePtr<kernel::KDevice> &dev, int depth, bool isStack = false) {
  if (!dev) return;

  kstd::string_view name = dev->Name();
  if (name.empty()) name = "<unnamed>";
  kstd::string_view hwid = dev->hardwareId;
  if (hwid.empty()) hwid = "-";
  const kstd::string_view driverName = dev->driverObject
                                           ? kstd::string_view(dev->driverObject->name)
                                           : kstd::string_view("<none>");

  const char *prefix = isStack ? "@-- " : "|-- ";
  KE_Print("{}{} {} [type={} hwid={} driver={}]", Indent(depth), prefix, name, GetDeviceTypeName(dev->deviceType), hwid, driverName);

  // 1. Recurse into the FDO stacked on top of this PDO (vertical)
  if (dev->attachedDevice) {
    DumpDevice(dev->attachedDevice, depth + 1, true);
  }

  // 2. Recurse into children if this is a bus (horizontal)
  if (dev->deviceType == kernel::device_type::Bus) {
    if (const auto bus = dev.As<kernel::KDeviceBus>()) {
      for (const auto &child: bus->children) {
        if (child) DumpDevice(child, depth + 1, false);
      }
    }
  }
}

[[maybe_unused]] void DumpDeviceTree() {
  const auto root = KE_PNP_GetRootDevice();
  if (!root) return;

  KE_Print("Device stack (devnode tree):");
  DumpDevice(root, 0, false);
}

}// namespace

UNDOS_KERNEL_API_DEF [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info) noexcept {
  kernel::crt::run_global_constructors();
  g_page_size = boot_info.page_size;

  HAL_PLATFORM_Init(boot_info, KE_VMM_GetKernelAddressSpace());
  if (!kernel::crt::global_constructors_ok()) {
    HAL_PLATFORM_Panic("Kernel global constructors did not run\r\n", __FILE__, __LINE__);
  }
  KE_Print("UnDOS Kernel INIT with page size {}", g_page_size);
  KE_Print("Command line: {}", boot_info.command_line);

  kernel::vmm::init();
  kernel::objectmanager::init();
  kernel::vmm::late_init();

  const kernel::CommandLineParser parser(boot_info.command_line);
  RegisterBootMappedRegions(boot_info);
  kernel::module::RegisterBootModules(boot_info);
  MemoryReclaim_BootStructures();

  kernel::driver::init();
  HAL_VMM_FinalizeInit();
  kernel::prof::prof_init();
  kernel::sched::init_bsp();
  kernel::dispatch::init();
  if (parser.has_key("profile_dump_every")) {
    uint32_t seconds = 0;
    for (const char c: parser.get("profile_dump_every", {})) {
      if (c < '0' || c > '9') break;
      seconds = seconds * 10 + static_cast<uint32_t>(c - '0');
    }
    kernel::prof::prof_schedule_dump(seconds);
  }
  HAL_PLATFORM_InitializeSystemTimer();
  HAL_CPU_EnableInterrupts();
  kernel::event::init();
  kernel::pnp::init();
  kernel::resource::init();
  kernel::input::init();
  kernel::vfs::init();
  kernel::initial::init_boot_device_volume();
  kernel::configuration_manager::init();
  kernel::driver::LoadBootDrivers();

  // Tell the HAL the Object Manager is ready.
  // HAL can now safely allocate object handles for PCI/PCIe buses and drivers.
  // When no PCI bus is present, the HAL reports a legacy ISA root device, which
  // cascades through the PnP manager: bind bus_isa -> create IsaBusFdo ->
  // StartDevice -> auto-enumerate children -> bind disk_ata.
  HAL_PLATFORM_AfterObjectManager();

  // Device processing (bind/start/enumerate/class-trigger) now happens
  // asynchronously on the PnP worker thread; wait for the queue to fully
  // drain so the root-fs lookup below still sees a fully-started tree.
  KE_PNP_WaitForQuiescence();

  // Bring every CPU the HAL reports online. On a uniprocessor HAL this is a
  // no-op and online_count() stays 1.
  kernel::smp::start_secondary_cpus();
  KE_Print("SMP: {} of {} CPU(s) online",
           kernel::cpu::online_count(),
           HAL_PLATFORM_GetCpuCount());

  kernel::KObjectPtr<kernel::KDeviceObject> rootDeviceObject{};
  const auto rootSelector = parser.get("root", {});
  if (!parser.has_key("root")) {
    HAL_PLATFORM_Panic("No root filesystem specified\r\n", __FILE__, __LINE__);
  } else if (rootSelector.substr(0, 5) == "UUID=") {
    uint32_t requestedId = 0;
    const auto volumeIdText = rootSelector.substr(5);
    if (!kernel::root_volume_selector::ParseVolumeId(volumeIdText, requestedId)) {
      KE_Print("Malformed root UUID selector '{}'; expected UUID=XXXX-XXXX", rootSelector);
      HAL_PLATFORM_Panic("Malformed root UUID selector\r\n", __FILE__, __LINE__);
    }

    const auto filesystemName = parser.get("rootfs", "FAT");
    if (filesystemName != "FAT") {
      KE_Print("Root UUID selectors require rootfs=FAT, not '{}'", filesystemName);
      HAL_PLATFORM_Panic("Incompatible filesystem for root UUID selector\r\n", __FILE__, __LINE__);
    }

    const auto filesystem = KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(
        KE_OB_GetFilesystemDirectory(), "FAT");
    if (!filesystem || !filesystem->ProbeVolumeId) {
      KE_Print("FAT volume-ID probe is unavailable");
      HAL_PLATFORM_Panic("Cannot resolve root UUID selector\r\n", __FILE__, __LINE__);
    }

    const auto result = kernel::root_volume_selector::FindVolumeById(
        KE_OB_GetDeviceDirectory(), requestedId, filesystem->ProbeVolumeId);
    switch (result.code) {
      case kernel::root_volume_selector::ResultCode::Selected:
        rootDeviceObject = result.deviceObject;
        KE_Print("Root UUID {:04x}-{:04x} selected volume '{}'",
                 requestedId >> 16, requestedId & 0xffff, rootDeviceObject->name);
        break;
      case kernel::root_volume_selector::ResultCode::NoMatch:
        KE_Print("No FAT volume matches root UUID {}", volumeIdText);
        HAL_PLATFORM_Panic("Root UUID volume not found\r\n", __FILE__, __LINE__);
        break;
      case kernel::root_volume_selector::ResultCode::Ambiguous:
        KE_Print("Root UUID {} matches {} FAT volumes", volumeIdText, result.matchCount);
        HAL_PLATFORM_Panic("Root UUID volume is ambiguous\r\n", __FILE__, __LINE__);
        break;
      case kernel::root_volume_selector::ResultCode::ProbeError:
        KE_Print("Failed to probe volume '{}' for root UUID: {}",
                 result.failingDeviceName, result.error.message());
        HAL_PLATFORM_Panic("Root UUID volume probe failed\r\n", __FILE__, __LINE__);
        break;
    }
  } else {
    rootDeviceObject = KE_OB_LookupObjectOfTypeWithRoot<kernel::KDeviceObject>(
        KE_OB_GetDeviceDirectory(), rootSelector);
    if (!rootDeviceObject) {
      KE_Print("Root device '{}' was not found", rootSelector);
      HAL_PLATFORM_Panic("Root device not found\r\n", __FILE__, __LINE__);
    }
  }

  if (rootDeviceObject) {
    // Keep the early-boot source mounted until root selection has succeeded.
    if (const auto status = kernel::configuration_manager::PrepareRootVolumeSwitch(); status) {
      KE_Print("Failed to prepare configuration hive for root switch: {}", status.message());
      HAL_PLATFORM_Panic("Cannot switch root while configuration hive is in use\r\n", __FILE__, __LINE__);
    }
    if (KE_VFS_Unmount("/")) {
      HAL_PLATFORM_Panic("Failed to unmount BootDeviceVolume from root\r\n", __FILE__, __LINE__);
    }

    if (KE_VFS_Mount(
            rootDeviceObject->device,
            parser.get("rootfs", "FAT"),
            "/",
            parser.get("rootops", {}))) {
      HAL_PLATFORM_Panic("Failed to mount root filesystem\r\n", __FILE__, __LINE__);
    }
  }

  // The root filesystem is mounted now: retain the existing index for the
  // same device, or load the selected volume's hive before normal startup.
  if (rootDeviceObject) {
    if (const auto status = kernel::configuration_manager::PromoteBootHive(rootDeviceObject->device); status) {
      KE_Print("Failed to promote LocalMachine hive: {}", status.message());
      HAL_PLATFORM_Panic("Failed to promote configuration hive\r\n", __FILE__, __LINE__);
    }
  }

  // Now that LocalMachine is rebound onto the writable root mount, load
  // drivers that don't need to be present to mount the root fs (e.g. the
  // VGA driver): declared with Start=Automatic instead of Start=Boot.
  // Subsystems load first so drivers can import their exports.
  kernel::subsystem::LoadAutomaticStartSubsystems();
  kernel::driver::LoadAutomaticStartDrivers();

  // Register the native ELF loader subsystem, a long-lived singleton used to
  // bootstrap user processes (see KE_USER_CreateProcess).
  if (const auto created = kernel::CreateKObject<kernel::KSubsystemObject>("NativeElfLoader")) {
    const auto &native_elf_loader = *created;
    native_elf_loader->loader_path = "/System/ELoader.elf";
    if (KE_OB_InsertObject(KE_OB_GetSubsystemDirectory(), native_elf_loader)) {
      KE_Print("Failed to register NativeElfLoader subsystem");
    }
  } else {
    KE_Print("Failed to allocate NativeElfLoader subsystem");
  }

  // Start init once PnP has bound every driver, so it sees all devices.
  KE_PNP_WaitForQuiescence();

  // Everything a real bus or the registry could report has now been bound, so
  // the last-resort pass can tell whether its job is still vacant. Today the
  // only such job is the console: load the fallback display driver (gfx_vga)
  // only when no accelerated driver registered a display.
  if (kernel::gfx::wait_for_drivers(); kernel::gfx::has_video_present_targets()) {
    KE_Print("Last-resort drivers: a display is already registered, skipping");
  } else {
    KE_Print("Last-resort drivers: no display registered, loading fallbacks");
    kernel::driver::LoadLastResortDrivers();
    KE_PNP_WaitForQuiescence();
    // PnP queues Started notifications; the event pump still has to enumerate
    // the fallback adapter. Userland selects the initial graphics mode.
    kernel::gfx::wait_for_drivers();
  }

  if (const auto status = kernel::gfx::init(); status) {
    KE_Print("GFX init failed: {}", status.message());
    HAL_PLATFORM_Panic("GFX init failed", __FILE__, __LINE__);
  }
  if (const auto status = kernel::user::init(); status) {
    KE_Print("USER init failed: {}", status.message());
    HAL_PLATFORM_Panic("USER init failed", __FILE__, __LINE__);
  }

  //DumpObjectTree(KE_OB_GetRootDirectory());
  //DumpDeviceTree();

  // Try and find the init
  KE_Print("Loading init...");
  if (const auto process = KE_USER_CreateProcess("/System/init.elf"); !process) {
    KE_Print("Failed to start init: {}", process.error().message());
  }

  if (const auto status = kernel::tty::detail::StartPumpThread(); status) {
    KE_Print("Failed to start TTY pump thread: {}", status.message());
    HAL_PLATFORM_Panic("Failed to start TTY pump thread", __FILE__, __LINE__);
  }

  KE_SCHED_Terminate();
  __builtin_unreachable();
}
