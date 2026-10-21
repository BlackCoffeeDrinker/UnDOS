#pragma once

#include "stdkrn.hpp"

#include <kernel/KObject.hpp>
#include <kernel/boot/boot_info.hpp>
#include <kernel/kerror.hpp>

#include <expected.hpp>
#include <string_view.hpp>

// Every loaded kernel module (the Kernel and HAL boot images, then each
// loadable subsystem) owns an export table; the registry is the one place
// that resolves a symbol name across all of them.
namespace kernel::module {

// Builds the "Kernel" and "HAL" modules from the Stage 1.5 boot symbols and
// registers them. Panics on a malformed handoff. Needs the heap and the
// object manager.
void RegisterBootModules(const BootInfoT &boot_info);

// Adds a module whose export table is sealed. Fails with AlreadyExists when
// the name, or any exported symbol, is already provided by another module.
kstd::error_code Register(const KObjectPtr<KModuleObject> &module);

[[nodiscard]] KObjectPtr<KModuleObject> Find(const kstd::string_view &name);

// Resolves `symbol` against every registered module, in registration order.
[[nodiscard]] kstd::expected<uintptr_t, kstd::error_code> Resolve(const kstd::string_view &symbol);

}// namespace kernel::module
