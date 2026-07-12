
#pragma once
#include <kernel/__core.hpp>

#include <chrono.hpp>

/**
 * @ingroup TIME
 * @brief Method KE_TIME_SetIncrement
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_TIME_SetIncrement,
    kstd::chrono::nanoseconds nanoseconds);

/**
 * @ingroup TIME
 * @brief Method KE_TIME_UpdateSystemTime
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_TIME_UpdateSystemTime);

/**
 * @ingroup TIME
 * @brief Method KE_TIME_GetSystemTime
 *
 */
UNDOS_KERNEL_PUBLIC_V1API([[nodiscard]] kstd::chrono::milliseconds, KE_TIME_GetSystemTime);
