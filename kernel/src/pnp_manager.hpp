
#pragma once
#include <kernel/kerror.hpp>
#include <kernel/kobject/KDriverObject.hpp>

namespace kernel::pnp {
KernelError init();
}

kernel::KernelError KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept;
