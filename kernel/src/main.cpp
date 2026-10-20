#include "stdkrn.hpp"


#include "configuration_manager.hpp"
#include "dispatch.hpp"
#include "driver_loader.hpp"
#include "event_broadcaster.hpp"
#include "gfx/gfx.hpp"
#include "initial/boot_device_vfs.hpp"
#include "input/input_manager.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"
#include "resource_manager.hpp"
#include "scheduler/CpuContext.hpp"
#include "scheduler/Scheduler.hpp"
#include "smp.hpp"
#include "user/user.hpp"
#include "vfs.hpp"
#include "vmm.hpp"

uint32_t g_page_size;

namespace {
class CommandLineParser {
  kstd::string_view _cmd;

  // Extract next token starting at index i
  kstd::string_view next_token(size_t &i) const {
    // Skip whitespace
    while (i < _cmd.size() && (_cmd[i] == ' ' || _cmd[i] == '\t')) {
      i++;
    }

    if (i >= _cmd.size()) {
      return {};
    }

    const auto start = i;

    // Quoted token
    if (_cmd[i] == '"') {
      i++;// skip opening quote
      const auto token_start = i;

      while (i < _cmd.size()) {
        if (_cmd[i] == '\\' && i + 1 < _cmd.size()) {
          i += 2;// skip escaped char
          continue;
        }
        if (_cmd[i] == '"') {
          const auto tok = _cmd.substr(token_start, i - token_start);
          i++;// skip closing quote
          return tok;
        }
        i++;
      }

      // Unterminated quote → return rest
      return _cmd.substr(token_start);
    }

    // Unquoted token
    while (i < _cmd.size() && _cmd[i] != ' ' && _cmd[i] != '\t') {
      i++;
    }

    return _cmd.substr(start, i - start);
  }

  // Split key=value
  static bool split_key_value(const kstd::string_view &tok,
                              kstd::string_view &key,
                              kstd::string_view &value) {
    const auto eq = tok.find('=');
    if (eq == kstd::string_view::npos) {
      return false;
    }

    key = tok.substr(0, eq);
    value = tok.substr(eq + 1);
    return true;
  }


  public:
  explicit CommandLineParser(const kstd::string_view &cmd) : _cmd(cmd) {}
  [[nodiscard]] kstd::string_view get_command_line() const { return _cmd; }

  [[nodiscard]] bool has_key(const kstd::string_view &key) const {
    size_t i = 0;
    while (true) {
      auto tok = next_token(i);
      if (tok.empty())
        return false;

      kstd::string_view k, v;
      if (split_key_value(tok, k, v)) {
        if (k == key)
          return true;
      } else {
        if (tok == key)
          return true;
      }
    }
  }

  [[nodiscard]] kstd::string_view get(const kstd::string_view &key,
                                      const kstd::string_view &default_value = "") const {
    size_t i = 0;
    while (true) {
      auto tok = next_token(i);
      if (tok.empty())
        return default_value;

      kstd::string_view k, v;
      if (split_key_value(tok, k, v)) {
        if (k == key)
          return v;
      } else {
        if (tok == key)
          return "on";
      }
    }
  }
};


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
  const CommandLineParser parser(boot_info.command_line);
  g_page_size = boot_info.page_size;

  KE_Print("UnDOS Kernel INIT with page size {}", g_page_size);
  KE_Print("Command line: {}", boot_info.command_line);

  HAL_PLATFORM_Init(boot_info, KE_VMM_GetKernelAddressSpace());
  kernel::vmm::init();
  if (const auto error = kernel::objectmanager::init(); error) {
    KE_Print("Failed to initialize object manager: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize object manager", __FILE__, __LINE__);
  }
  kernel::vmm::late_init();
  if (const auto error = kernel::driver::init(boot_info); error) {
    KE_Print("Failed to initialize driver loader: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize driver loader", __FILE__, __LINE__);
  }
  HAL_VMM_FinalizeInit();
  kernel::sched::init_bsp();
  if (const auto error = kernel::dispatch::init(); error) {
    KE_Print("Failed to initialize dispatch: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize dispatch", __FILE__, __LINE__);
  }
  HAL_PLATFORM_InitializeSystemTimer();
  HAL_CPU_EnableInterrupts();
  if (const auto error = kernel::event::init(); error) {
    KE_Print("Failed to initialize events: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize events", __FILE__, __LINE__);
  }
  if (const auto error = kernel::pnp::init(); error) {
    KE_Print("Failed to initialize PnP: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize PnP", __FILE__, __LINE__);
  }
  kernel::resource::init();
  if (const auto error = kernel::input::init(); error) {
    KE_Print("Failed to initialize input: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize input", __FILE__, __LINE__);
  }
  kernel::vfs::init();
  if (const auto error = kernel::initial::init_boot_device_volume(); error) {
    KE_Print("Failed to initialize boot volume: {}", error.message());
    HAL_PLATFORM_Panic("Failed to initialize boot volume", __FILE__, __LINE__);
  }
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
  //MemoryReclaim_BootStructures(boot_info);

  // Bring every CPU the HAL reports online. On a uniprocessor HAL this is a
  // no-op and online_count() stays 1.
  kernel::smp::start_secondary_cpus();
  KE_Print("SMP: {} of {} CPU(s) online",
           kernel::cpu::online_count(),
           HAL_PLATFORM_GetCpuCount());

  // Mount the root fs
  if (parser.has_key("root")) {
    // Find the root specified
    if (const auto root = KE_OB_LookupObjectOfTypeWithRoot<kernel::KDeviceObject>(
            KE_OB_GetDeviceDirectory(),
            parser.get("root"))) {
      // Drop the early-boot BootDeviceVolume mount before mounting the real
      // root filesystem in its place: "/" is the same mount point both
      // before and after this swap.
      if (KE_VFS_Unmount("/")) {
        HAL_PLATFORM_Panic("Failed to unmount BootDeviceVolume from root\r\n", __FILE__, __LINE__);
      }

      if (KE_VFS_Mount(
              root->device,
              parser.get("rootfs", "FAT"),
              "/",
              parser.get("rootops", {}))) {
        HAL_PLATFORM_Panic("Failed to mount root filesystem\r\n", __FILE__, __LINE__);
      }
    }
  } else {
    HAL_PLATFORM_Panic("No root filesystem specified\r\n", __FILE__, __LINE__);
  }

  // The root filesystem is mounted now: rebind LocalMachine from the
  // early-boot BootDeviceVolume path onto the writable root-mount path so
  // subsequent registry writes persist through the real disk stack.
  kernel::configuration_manager::PromoteBootHive();

  // Now that LocalMachine is rebound onto the writable root mount, load
  // drivers that don't need to be present to mount the root fs (e.g. the
  // VGA driver): declared with Start=Automatic instead of Start=Boot.
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
  if (kernel::gfx::wait_for_drivers(); KE_GFX_GetPrimaryDisplay()) {
    KE_Print("Last-resort drivers: a display is already registered, skipping");
  } else {
    KE_Print("Last-resort drivers: no display registered, loading fallbacks");
    kernel::driver::LoadLastResortDrivers();
    KE_PNP_WaitForQuiescence();
    // PnP queues Started notifications; the event pump still has to enumerate
    // the fallback adapter. Userland selects the initial graphics mode.
    kernel::gfx::wait_for_drivers();
  }

  kernel::user::init_raw_input();

  //DumpObjectTree(KE_OB_GetRootDirectory());
  //DumpDeviceTree();

  // Try and find the init
  KE_Print("Loading init...");
  if (const auto process = KE_USER_CreateProcess("/System/init.elf"); !process) {
    KE_Print("Failed to start init: {}", process.error().message());
  }

  // Thread 0 becomes a low-noise heartbeat. It must never return or shut the
  // CPU down, which would tear down the threads now running on it.
  for (;;) {
    KE_TTY_PumpAllAttachedDevices();
    HAL_CPU_Halt();
    KE_SCHED_Yield();
  }
}
