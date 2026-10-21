
#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>

#include <kernel/kobject/KDriverObject.hpp>

#include <string_view.hpp>

namespace kernel {
using DriverLoadResult = kstd::expected<KObjectPtr<KDriverObject>, kstd::error_code>;
}

/**
 * @ingroup DRIVER
 * @brief Method KE_DRIVER_Load
 *
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::DriverLoadResult,
    KE_DRIVER_Load,
    const kstd::string_view &path);
