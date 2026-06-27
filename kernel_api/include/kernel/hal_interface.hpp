#pragma once

#include <kernel/__core.hpp>

#include <strfmt.hpp>
#include <string_view.hpp>
#include <utility.hpp>

#include "entry.hpp"

#include "memory/virtual_memory.hpp"

UNDOS_HAL_API void HAL_Platform_Init(const kernel::BootInfoT &boot_info) noexcept;
UNDOS_HAL_API void HAL_Platform_InitializeSystemTimer() noexcept;
UNDOS_HAL_API void HAL_Platform_DetectSystemBus() noexcept;
UNDOS_HAL_API [[noreturn]] void HAL_Platform_Panic(const char *message, const char *file, int line) noexcept;

UNDOS_HAL_API void early_print(const char *str);
UNDOS_HAL_API void early_print_char(char c);

template<typename... Args>
void early_print_fmt(const kstd::string_view &fmt, Args &&...args) { kstd::format_dst(early_print_char, fmt, args...); }


// region Physical Memory Manager API
UNDOS_HAL_API uintptr_t HAL_PMM_Allocate_Frames(size_t count) noexcept;
UNDOS_HAL_API void HAL_PMM_Free_Frame(uintptr_t physical_address) noexcept;
UNDOS_HAL_API void HAL_PMM_Reserve_Region(uintptr_t base, size_t length) noexcept;
// endregion

// region Virtual Memory Manager API
UNDOS_HAL_API void HAL_VMM_EarlyInit(const kernel::BootInfoT &boot_info) noexcept;// < No Object Manager is available here
UNDOS_HAL_API void HAL_VMM_FinalizeInit() noexcept;                                 // < Object Manager is available here
UNDOS_HAL_API bool HAL_VMM_MapPage(uintptr_t virt, uintptr_t phys, kernel::vmm::ProtectFlags flags) noexcept;
UNDOS_HAL_API void HAL_VMM_UnmapPage(uintptr_t virt) noexcept;
UNDOS_HAL_API void HAL_VMM_Flush(uintptr_t virt) noexcept;// Reloads CR3
// endregion
