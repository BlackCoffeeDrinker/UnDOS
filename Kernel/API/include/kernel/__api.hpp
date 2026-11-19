#pragma once

// Export macros only, so low-level headers (KStd) can declare kernel exports
// without pulling in the rest of <kernel/__core.hpp>.
//
// The kernel and its components build with -fvisibility=hidden: a symbol is
// part of the kernel's exported interface only when it is declared with
// UNDOS_KERNEL_PUBLIC_V1API (or UNDOS_HAL_PUBLIC_V1API). Everything else is
// private. The only other exports are the C++ ABI symbols listed in
// Kernel/Source/kernel_exports_allowlist.txt; the kernel build checks this.

#define UNDOS_KERNEL_API_DEF __attribute__((visibility("default"), used))
#define UNDOS_HAL_API_DEF __attribute__((visibility("default"), used))
// C++ ABI symbols with fixed names (operator delete, __cxa_*) that loadable
// modules import; each must be on the export allowlist.
#define UNDOS_KERNEL_CPP_API __attribute__((visibility("default"), used))

#define UNDOS_KERNEL_PUBLIC_V1API(return_type, func_name, ...) \
  UNDOS_KERNEL_API_DEF return_type func_name(__VA_ARGS__) noexcept asm(#func_name)

#define UNDOS_HAL_PUBLIC_V1API(return_type, func_name, ...) \
  UNDOS_HAL_API_DEF return_type func_name(__VA_ARGS__) noexcept asm(#func_name)
