
#pragma once
#include <kernel/kerror.hpp>
#include <kernel/kobject/KDriverObject.hpp>

namespace kernel::pnp {
void init();
}

kstd::error_code KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept;
