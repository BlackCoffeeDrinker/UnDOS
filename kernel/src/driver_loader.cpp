
#include "driver_loader.hpp"

#include "elf_loader.hpp"
#include "elf_segment_loader.hpp"
#include "module/symbol_table.hpp"
#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/configuration_manager.hpp>
#include <kernel/elf.hpp>
#include <memory.hpp>
#include <new.hpp>
#include <system_error.hpp>

namespace {
kernel::driver::SymbolTable g_SymbolTable;

enum class ElfError {
  Success = 0,
  InvalidHeader,
  BoundsViolation,
  RelocationFailure,
  AllocationFailure
};

// Plain function pointer wrapping g_SymbolTable::Resolve, since cfunc only
// wraps C-style function pointers (no captures).
uintptr_t ResolveSymbol(kstd::string_view name) {
  return g_SymbolTable.Resolve(name);
}

// Copies a matched ".driver_name" section's bytes into the static_string
// pointed to by `context`, before elf_loader frees its temporary read
// buffer. The name is captured separately since KObject::name is const and
// can only be set once the KDriverObject is actually constructed.
void CaptureDriverName(void *context, kstd::string_view name, const uint8_t *data, size_t) {
  if (name != ".driver_name") return;
  auto *name_out = static_cast<kstd::static_string<64> *>(context);
  *name_out = reinterpret_cast<const char *>(data);
}

// Enumerates every child under LocalMachine's "/Drivers" key and calls
// KE_DRIVER_Load(ImagePath) for each one whose "Start" value matches
// `startFilter` exactly. Shared between the pre-root-mount "Boot" pass
// (kernel::driver::init()) and the post-root-mount "Automatic" pass
// (kernel::driver::LoadAutomaticStartDrivers()).
void LoadDriversWithStart(const kstd::string_view &startFilter, const kstd::string_view &logLabel) {
  early_print_fmt("Loading {} drivers...\r\n", logLabel);

  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    early_print_fmt("Loading {} drivers: failed to open LocalMachine\r\n", logLabel);
    early_print_fmt("Loading {} drivers complete.\r\n", logLabel);
    return;
  }

  const auto driversKey = KE_CM_OpenKey(localMachine, "Drivers");
  if (!driversKey) {
    early_print_fmt("Loading {} drivers: no Drivers key under LocalMachine\r\n", logLabel);
    early_print_fmt("Loading {} drivers complete.\r\n", logLabel);
    return;
  }

  for (uint32_t index = 0;; ++index) {
    kstd::string_view childName;
    if (!KE_CM_EnumKey(driversKey, index, childName)) {
      break;
    }

    const auto driverKey = KE_CM_OpenKey(driversKey, childName);
    if (!driverKey) {
      continue;
    }

    const auto startKey = KE_CM_OpenKey(driverKey, "Start");
    if (!startKey) {
      continue;
    }

    kstd::string_view startValue;
    if (!KE_CM_GetKeyValueString(startKey, startValue) || startValue != startFilter) {
      continue;
    }

    const auto imagePathKey = KE_CM_OpenKey(driverKey, "ImagePath");
    if (!imagePathKey) {
      early_print_fmt("{}-start driver '{}' has no ImagePath, skipping\r\n", logLabel, childName);
      continue;
    }

    kstd::string_view imagePath;
    if (!KE_CM_GetKeyValueString(imagePathKey, imagePath) || imagePath.empty()) {
      early_print_fmt("{}-start driver '{}' has empty/invalid ImagePath, skipping\r\n", logLabel, childName);
      continue;
    }

    early_print_fmt("Loading {}-start driver '{}' from '{}'\r\n", logLabel, childName, imagePath);
    KE_DRIVER_Load(imagePath);
  }

  early_print_fmt("Loading {} drivers complete.\r\n", logLabel);
}

}// namespace

namespace kernel::driver {
void init(const BootInfoT &boot_info) {
  early_print_fmt("Loading symbols...\r\n");
  for (const auto &sym: boot_info.boot_symbols) {
    if (sym.name.empty()) continue;
    g_SymbolTable.Register(sym.name, static_cast<uintptr_t>(sym.address));
  }

  // Load boot-start drivers declared under LocalMachine's /Drivers key
  // (Start == "Boot", ImagePath = VFS path to the driver ELF). These are
  // needed to bring up the real disk stack and mount the root filesystem.
  LoadDriversWithStart("Boot", "boot");
}

void LoadAutomaticStartDrivers() {
  // Drivers that aren't needed to mount the root filesystem (e.g. gfx_vga)
  // are deferred until the real root fs is mounted and LocalMachine has been
  // rebound onto it, so their ImagePath resolves through the writable mount.
  LoadDriversWithStart("Automatic", "automatic");
}
}// namespace kernel::driver

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDriverObject> KE_DRIVER_Load(const kstd::string_view &path) noexcept {
  if (path.empty()) {
    return nullptr;
  }

  kstd::static_string<64> captured_name = "";

  kernel::elf::LoadPolicy policy;
  policy.relocatable = true;
  policy.context = &captured_name;
  policy.resolve_symbol = kernel::cfunc<uintptr_t(kstd::string_view)>(ResolveSymbol);
  policy.capture_section = kernel::cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)>(CaptureDriverName);

  const auto result = kernel::elf::LoadElfFromVfs(path, policy);
  if (!result.ok) {
    early_print_fmt("KE_DRIVER_Load: Failed to load driver from path {}\r\n", path);
    return nullptr;
  }

  auto driver_object = kernel::CreateKObject<kernel::KDriverObject>(captured_name);
  if (!driver_object) {
    early_print_fmt("KE_DRIVER_Load: Failed to create driver object\r\n");
    return nullptr;
  }
  
  // Give the driver its own configuration key under LocalMachine, e.g.
  // "/LocalMachine/Drivers/<name>". LocalMachine is disk-backed from the
  // start of boot (first via BootDeviceVolume, then rebound onto the real
  // root mount), so there is no separate volatile "Boot" hive to target.
  kernel::KConfigurationKeyPtr driverConfig = nullptr;
  if (const auto driverConfigRoot = KE_CM_OpenRoot("LocalMachine")) {
    if (const auto driversKey = KE_CM_CreateKey(driverConfigRoot, "Drivers", kernel::KConfigurationKeyAccess::ReadWrite)) {
      driverConfig = KE_CM_CreateKey(driversKey, driver_object->name, kernel::KConfigurationKeyAccess::ReadWrite);
    }
  }

  driver_object->load_base = result.load_base;
  driver_object->total_size = result.total_size;
  driver_object->entry_point = reinterpret_cast<void (*)(kernel::KObjectPtr<kernel::KDriverObject> &, const kernel::KConfigurationKeyPtr &)>(result.entry_point.value);
  driver_object->entry_point(driver_object, driverConfig);

  // TODO: Remove the entry point from memory once it's done executing
  
  KE_OB_InsertObject(KE_OB_GetDriverDirectory(), driver_object);
  KE_PNP_RegisterDriver(driver_object);
  return driver_object;
}
