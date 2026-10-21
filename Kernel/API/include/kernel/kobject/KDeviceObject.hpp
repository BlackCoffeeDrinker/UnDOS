#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

/**
 * @brief Represents a device object (PDO, FDO, etc.)
 */
struct KDeviceObject : KObjectT<KDeviceObject, 1, TYPE_DEVICE> {
  using KObjectT::KObjectT;
  KDevicePtr<KDevice> device;
};

template<>
struct ObjectTypeOf<TYPE_DEVICE> {
  using type = KDeviceObject;
};
}// namespace kernel
