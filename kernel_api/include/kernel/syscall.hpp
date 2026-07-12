#pragma once

#include <kernel/__core.hpp>

// Syscall numbers understood by KE_SYSCALL_Dispatch's jump table.
constexpr uint32_t SYS_EXIT = 0;
constexpr uint32_t SYS_WRITE = 1;

/**
 * @ingroup SYSCALL
 * @brief Method KE_SYSCALL_Dispatch
 *
 * Platform-agnostic entry point invoked by the HAL's int 0x80 trap handler.
 * Dispatches on `number` to the matching kernel syscall implementation.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    int32_t,
    KE_SYSCALL_Dispatch,
    uint32_t number, uint32_t arg0, uint32_t arg1, uint32_t arg2);
