
#include "driver_loader.hpp"

#include "elf_loader.hpp"
#include "elf_segment_loader.hpp"
#include "module/symbol_table.hpp"
#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
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

// Copies a matched ".driver_name" section's bytes into the KDriverObject
// pointed to by `context`, before elf_loader frees its temporary read
// buffer.
void CaptureDriverName(void *context, kstd::string_view name, const uint8_t *data, size_t) {
  if (name != ".driver_name") return;
  const auto *driver_object = static_cast<kernel::KObjectPtr<kernel::KDriverObject> *>(context);
  (*driver_object)->name = reinterpret_cast<const char *>(data);
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
    auto module = CreateKObject<kernel::KModuleObject>(driver.name);
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

  auto driver_object = kernel::CreateKObject<kernel::KDriverObject>("<tmpname>");
  if (!driver_object) {
    early_print_fmt("KE_DRIVER_Load: Failed to create driver object\r\n");
    return nullptr;
  }

  kernel::elf::LoadPolicy policy;
  policy.relocatable = true;
  policy.context = &driver_object;
  policy.resolve_symbol = kernel::cfunc<uintptr_t(kstd::string_view)>(ResolveSymbol);
  policy.capture_section = kernel::cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)>(CaptureDriverName);

  const auto result = kernel::elf::LoadElfFromVfs(path, policy);
  if (!result.ok) {
    early_print_fmt("KE_DRIVER_Load: Failed to load driver from path {}\r\n", path);
    return nullptr;
  }

  driver_object->load_base = result.load_base;
  driver_object->total_size = result.total_size;
  driver_object->entry_point = reinterpret_cast<void (*)(kernel::KObjectPtr<kernel::KDriverObject> &)>(result.entry_point.value);
  driver_object->entry_point(driver_object);

  // TODO: Remove the entry point from memory once it's done executing


  KE_OB_InsertObject(KE_OB_GetDriverDirectory(), driver_object);
  KE_PNP_RegisterDriver(driver_object);
  return driver_object;
}
