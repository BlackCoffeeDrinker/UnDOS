
#pragma once
#include <kernel/KObject.hpp>

/**
 * @ingroup GFX
 * @brief Method KE_GFX_RegisterGraphicsAdapter
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KGraphicsAdapter>,
    KE_GFX_RegisterGraphicsAdapter,
    const kernel::KObjectPtr<kernel::KDriverObject> &driver,
    const kernel::KDevicePtr<kernel::KDevice> &backingDevice,
    const kstd::string_view &name);

/**
 * @ingroup GFX
 * @brief Method KE_GFX_RegisterDisplay
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDisplay>,
    KE_GFX_RegisterDisplay,
    const kernel::KObjectPtr<kernel::KGraphicsAdapter> &adapter,
    const kstd::string_view &name);

/**
 * @ingroup GFX
 * @brief Method KE_GFX_CreateSurface
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KSurface>,
    KE_GFX_CreateSurface,
    const kernel::KObjectPtr<kernel::KDisplay> &display, size_t mode);
