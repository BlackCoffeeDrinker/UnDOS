#pragma once

#include <kernel/__core.hpp>

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
    uint32_t number, uint32_t size, void *data, uint32_t flags);
