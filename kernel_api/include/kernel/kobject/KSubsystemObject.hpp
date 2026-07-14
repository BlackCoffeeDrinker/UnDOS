#pragma once

#include <kernel/__core.hpp>
#include <kernel/data_buffer.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KSubsystemObject : KObjectT<KSubsystemObject, 1, TYPE_SUBSYSTEM> {
  kstd::static_string<255> loader_path;// path to the user-mode loader image
  DataBuffer systemExtension;          // opaque subsystem-specific data
  VirtualAddress loader_entry{0};      // loader entry point (user-mode)
};

template<>
struct ObjectTypeOf<TYPE_SUBSYSTEM> {
  using type = KSubsystemObject;
};

}// namespace kernel
