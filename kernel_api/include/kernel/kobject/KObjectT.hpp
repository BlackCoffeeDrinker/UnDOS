#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObject.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

template<typename T, size_t Version, ObjectType ObjectTypeId>
struct KObjectT : KObject, Versioned<T, Version> {
  static constexpr ObjectType Type = ObjectTypeId;

  constexpr KObjectT() noexcept : KObject(Type) {}
};

} // namespace kernel
