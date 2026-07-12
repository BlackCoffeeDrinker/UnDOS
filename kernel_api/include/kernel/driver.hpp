
#pragma once

#include <kernel/__core.hpp>

#include <kernel/kobject/KDriverObject.hpp>

#include <string_view.hpp>

/**
 * @ingroup DRIVER
 * @brief Method KE_DRIVER_Load
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KDriverObject>,
    KE_DRIVER_Load,
    const kstd::string_view &path);
