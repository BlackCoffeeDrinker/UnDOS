#pragma once


#include <strfmt.hpp>
#include <string_view.hpp>
#include <utility.hpp>

#include <kernel/__core.hpp>
#include <kernel/entry.hpp>
#include <kernel/memory/virtual_memory.hpp>

UNDOS_HAL_API void HAL_PLATFORM_Init(const kernel::BootInfoT &boot_info) noexcept;
UNDOS_HAL_API void HAL_PLATFORM_InitializeSystemTimer() noexcept;
UNDOS_HAL_API void HAL_PLATFORM_AfterObjectManager() noexcept;
UNDOS_HAL_API [[noreturn]] void HAL_PLATFORM_Panic(const char *message, const char *file, int line) noexcept;
UNDOS_HAL_API void HAL_CPU_Halt() noexcept;
UNDOS_HAL_API [[noreturn]] void HAL_PLATFORM_Shutdown() noexcept;

// region Port I/O API
UNDOS_HAL_API void HAL_IO_Out8(uint16_t port, uint8_t val) noexcept;
UNDOS_HAL_API uint8_t HAL_IO_In8(uint16_t port) noexcept;
UNDOS_HAL_API void HAL_IO_Delay() noexcept;
// endregion

// region CPU Execution Environment API
UNDOS_HAL_API uint32_t HAL_PLATFORM_GetCpuCount() noexcept;
UNDOS_HAL_API void HAL_CPU_ReloadContext() noexcept;
// endregion

UNDOS_HAL_API void early_print(const char *str);
UNDOS_HAL_API void early_print_char(char c);

template<typename... Args>
void early_print_fmt(const kstd::string_view &fmt, Args &&...args) { kstd::format_dst(early_print_char, fmt, args...); }

// region Physical Memory Manager API
UNDOS_HAL_API kernel::PhysicalAddress HAL_PMM_AllocateFrames(size_t count) noexcept;
UNDOS_HAL_API kernel::PhysicalAddress HAL_PMM_AllocateFramesDMA(size_t count) noexcept;
UNDOS_HAL_API void HAL_PMM_FreeFrames(kernel::PhysicalAddress base, size_t count) noexcept;
UNDOS_HAL_API void HAL_PMM_ReserveRegion(kernel::PhysicalAddress base, size_t length) noexcept;
// endregion

// region Virtual Memory Manager API
UNDOS_HAL_API void HAL_VMM_EarlyInit(const kernel::BootInfoT &boot_info) noexcept;// < No Object Manager is available here
UNDOS_HAL_API void HAL_VMM_FinalizeInit() noexcept;                               // < Object Manager is available here
UNDOS_HAL_API kernel::PhysicalAddress HAL_VMM_GetCurrentTranslationRoot() noexcept;
UNDOS_HAL_API kernel::PhysicalAddress HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress virt) noexcept;// < Walks the active page structures to return the raw physical frame.
UNDOS_HAL_API void HAL_VMM_SwitchAddressSpace(kernel::PhysicalAddress new_root) noexcept;              // < Loads a new translation root (e.g. CR3, TTBR, satp).
UNDOS_HAL_API bool HAL_VMM_MapPage(kernel::VirtualAddress virt, kernel::PhysicalAddress phys, kernel::vmm::ProtectFlags flags) noexcept;
UNDOS_HAL_API void HAL_VMM_UnmapPage(kernel::VirtualAddress virt) noexcept;
UNDOS_HAL_API void HAL_VMM_Flush(kernel::VirtualAddress virt) noexcept;// Reloads CR3
// endregion
