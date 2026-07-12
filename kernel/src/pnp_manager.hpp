
#pragma once
#include <kernel/kobject/KDriverObject.hpp>

namespace kernel::pnp {
void init();
}

void KE_PNP_RegisterDriver(const kernel::KObjectPtr<kernel::KDriverObject> &driver) noexcept;
