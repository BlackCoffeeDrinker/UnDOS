#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KModuleObject : KObjectT<KModuleObject, 1, TYPE_MODULE> {
  PhysicalAddress base_physical;
  size_t length;
};

} // namespace kernel
