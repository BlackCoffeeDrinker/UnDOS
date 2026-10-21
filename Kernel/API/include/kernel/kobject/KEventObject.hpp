#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KEventObject : KObjectT<KEventObject, 1, TYPE_EVENT> {
  using KObjectT::KObjectT;

  KDispatcherHeader header;
};

template<>
struct ObjectTypeOf<TYPE_EVENT> {
  using type = KEventObject;
};

} // namespace kernel
