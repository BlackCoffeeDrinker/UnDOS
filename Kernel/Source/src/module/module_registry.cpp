#include "module_registry.hpp"

#include <Kernel.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/spinlock.hpp>

#include <mutex.hpp>
#include <utility.hpp>
#include <vector.hpp>

namespace {
kernel::SpinLock g_lock;
kstd::vector<kernel::KObjectPtr<kernel::KModuleObject>> g_modules;

kstd::expected<uintptr_t, kstd::error_code> ResolveLocked(const kstd::string_view &symbol) {
  for (const auto &module: g_modules) {
    if (const auto address = module->exports.Find(symbol)) return address->value;
  }
  return kstd::unexpected(kernel::KernelError::NotFound);
}

kernel::KObjectPtr<kernel::KModuleObject> BuildBootModule(const kernel::BootInfoT &boot_info, const kstd::string_view &name, size_t first, size_t last) {
  auto created = kernel::CreateKObject<kernel::KModuleObject>(kstd::static_string<64>(name));
  if (!created) HAL_PLATFORM_Panic("Failed to allocate a boot module", __FILE__, __LINE__);
  auto &exports = (*created)->exports;
  exports.Reserve(last - first, boot_info.boot_symbol_names_used);
  for (size_t i = first; i < last; ++i) {
    if (exports.Add(boot_info.SymbolName(i), boot_info.boot_symbols[i].address)) {
      HAL_PLATFORM_Panic("Failed to record a boot module export", __FILE__, __LINE__);
    }
  }
  if (const auto status = exports.Seal()) {
    KE_Print("Boot module '{}' has duplicate exports: {}", name, status.message());
    HAL_PLATFORM_Panic("Failed to seal a boot module export table", __FILE__, __LINE__);
  }
  return *created;
}
}// namespace

namespace kernel::module {
void RegisterBootModules(const BootInfoT &boot_info) {
  const size_t total = boot_info.boot_symbol_count;
  const size_t kernelCount = boot_info.boot_kernel_symbol_count;
  if (total > boot_info.boot_symbols.size() || kernelCount > total) {
    HAL_PLATFORM_Panic("Corrupt boot symbol handoff", __FILE__, __LINE__);
  }

  const auto kernel = BuildBootModule(boot_info, "Kernel", 0, kernelCount);
  const auto hal = BuildBootModule(boot_info, "HAL", kernelCount, total);
  for (const auto &boot_module: {kernel, hal}) {
    if (const auto status = Register(boot_module)) {
      KE_Print("Failed to register boot module '{}': {}", boot_module->name, status.message());
      HAL_PLATFORM_Panic("Failed to register boot modules", __FILE__, __LINE__);
    }
  }
}

kstd::error_code Register(const KObjectPtr<KModuleObject> &module) {
  if (!module->exports.sealed()) return KernelError::InvalidArgument;

  {
    kstd::unique_lock lock(g_lock);
    for (const auto &existing: g_modules) {
      if (existing->name == module->name) return KernelError::AlreadyExists;
    }
    for (size_t i = 0; i < module->exports.size(); ++i) {
      if (ResolveLocked(module->exports.NameAt(i))) {
        KE_Print("Module '{}' exports duplicate symbol '{}'", module->name, module->exports.NameAt(i));
        return KernelError::AlreadyExists;
      }
    }
    g_modules.push_back(module);
  }

  if (const auto status = KE_OB_InsertObject(KE_OB_GetModuleDirectory(), module)) {
    kstd::unique_lock lock(g_lock);
    g_modules.pop_back();
    return status;
  }
  return {};
}

KObjectPtr<KModuleObject> Find(const kstd::string_view &name) {
  kstd::unique_lock lock(g_lock);
  for (const auto &module: g_modules) {
    if (kstd::string_view(module->name) == name) return module;
  }
  return nullptr;
}

kstd::expected<uintptr_t, kstd::error_code> Resolve(const kstd::string_view &symbol) {
  kstd::unique_lock lock(g_lock);
  return ResolveLocked(symbol);
}
}// namespace kernel::module
