#include "stdkrn.hpp"

#include "driver_loader.hpp"
#include "initial/boot_modules_vfs.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"
#include "resource_manager.hpp"
#include "scheduler/Scheduler.hpp"
#include "vfs.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>

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

void DumpObjectTree(const kernel::KObjectPtr<kernel::KObject> &obj, int depth = 0) {
  if (!obj) return;

  for (int i = 0; i < depth; ++i) { early_print("  "); }

  kstd::string_view name = obj->name;
  if (name.empty() && depth == 0) {
    name = "/";
  }

  early_print_fmt("|-- {} ({})\n\r", name, GetObjectTypeName(obj->type));

  if (const auto directory = obj.As<kernel::KDirectoryObject>()) {
    directory->children.each([&](auto entry) {
      DumpObjectTree(entry, depth + 1);
    });
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

  for (int i = 0; i < depth; ++i) { early_print("  "); }

  kstd::string_view name = dev->Name();
  if (name.empty()) name = "<unnamed>";
  kstd::string_view hwid = dev->hardwareId;
  if (hwid.empty()) hwid = "-";
  const kstd::string_view driverName = dev->driverObject
                                           ? kstd::string_view(dev->driverObject->name)
                                           : kstd::string_view("<none>");

  const char *prefix = isStack ? "@-- " : "|-- ";
  early_print_fmt("{} {} [type={} hwid={} driver={}]\n\r", prefix, name, GetDeviceTypeName(dev->deviceType), hwid, driverName);

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

void DumpDeviceTree() {
  const auto root = KE_PNP_GetRootDevice();
  if (!root) return;

  early_print("\n\rDevice stack (devnode tree):\n\r");
  DumpDevice(root, 0, false);
}

}// namespace

UNDOS_KERNEL_API_DEF [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info) noexcept {
  const CommandLineParser parser(boot_info.command_line);
  g_page_size = boot_info.page_size;

  HAL_PLATFORM_Init(boot_info);
  kernel::vmm::init();
  kernel::objectmanager::init();
  kernel::vmm::late_init();
  HAL_VMM_FinalizeInit();
  kernel::sched::init();
  HAL_PLATFORM_InitializeSystemTimer();
  HAL_CPU_EnableInterrupts();
  kernel::pnp::init();
  kernel::resource::init();
  kernel::vfs::init();
  kernel::driver::init(boot_info);
  kernel::initial::init();

  // Tell the HAL the Object Manager is ready.
  // HAL can now safely allocate object handles for PCI/PCIe buses and drivers.
  // When no PCI bus is present, the HAL reports a legacy ISA root device, which
  // cascades through the PnP manager: bind bus_isa -> create IsaBusFdo ->
  // StartDevice -> auto-enumerate children -> bind disk_ata.
  HAL_PLATFORM_AfterObjectManager();
  //MemoryReclaim_BootStructures(boot_info);

  // TODO: Bring extra CPUs online
  if (HAL_PLATFORM_GetCpuCount() > 1) {
  }

  // Mount the root fs
  if (parser.has_key("root")) {
    // Find the root specified
    if (const auto root = KE_OB_LookupObjectOfTypeWithRoot<kernel::KDeviceObject>(
            KE_OB_GetDeviceDirectory(),
            parser.get("root"))) {
      if (!KE_VFS_Mount(
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

  // Try and find the init

  DumpObjectTree(KE_OB_GetRootDirectory());
  DumpDeviceTree();
  early_print("Done\r\n");


  // Thread 0 becomes a low-noise heartbeat. It must never return or shut the
  // CPU down, which would tear down the threads now running on it.
  for (;;) {
    HAL_CPU_Halt();
    KE_SCHED_Yield();
  }
}
