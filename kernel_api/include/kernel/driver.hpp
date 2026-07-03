
#pragma once

#include <kernel/__core.hpp>

#include <kernel/kobject/KDriverObject.hpp>

#include <string_view.hpp>

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KDriverObject> KE_DRIVER_Load(const kstd::string_view &path);
