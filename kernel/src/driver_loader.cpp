
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

}// namespace

namespace kernel::driver {
void init(const BootInfoT &boot_info) {
  early_print_fmt("Loading symbols...\r\n");
  for (const auto &sym: boot_info.boot_symbols) {
    if (sym.name.empty()) continue;
    g_SymbolTable.Register(sym.name, static_cast<uintptr_t>(sym.address));
  }

  // Load all boot drivers
  early_print_fmt("Loading boot drivers...\r\n");
  const auto base = KE_OB_LookupObjectOfType<KDirectoryObject>("/System/Initial/BootModules");
  for (const auto &driver: boot_info.boot_modules) {
    if (driver.name.empty() || driver.base_physical == 0 || driver.length == 0) continue;
    
    auto module = CreateKObject<KModuleObject>(driver.name);
    module->base_physical = driver.base_physical;
    module->length = driver.length;
    KE_OB_InsertObject(base, module);
  }

  early_print_fmt("Loading boot drivers complete.\r\n");
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
  
  // Give the driver its own configuration key, e.g. "/Boot/Drivers/<name>",
  // scoped under the volatile "Boot" hive (see kernel::configuration_manager::init()).
  // "Boot" is always registered by this point (configuration_manager::init()
  // now runs before any driver is loaded), so this should never fail in
  // practice. If "Boot" has already been promoted/dropped (see
  // kernel::configuration_manager::PromoteBootHive()) - i.e. a driver is
  // being (re)loaded at runtime, after boot - fall back to targeting
  // "LocalMachine" directly instead.
  kernel::KConfigurationKeyPtr driverConfig = nullptr;
  auto driverConfigRoot = KE_CM_OpenRoot("Boot");
  if (!driverConfigRoot) {
    driverConfigRoot = KE_CM_OpenRoot("LocalMachine");
  }
  if (driverConfigRoot) {
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
