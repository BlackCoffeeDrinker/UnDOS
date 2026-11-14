
#pragma once
#include <kernel/kerror.hpp>
#include <kernel/kobject/KDriverObject.hpp>

namespace kernel::pnp {
void init();
// Reports one ROOT child per LocalMachine/Enum/Root subkey (HardwareId value).
void EnumerateRootFromHive();
}

kstd::error_code KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept;
