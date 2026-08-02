#pragma once

#include <kernel/__core.hpp>
#include <kernel/common/DoubleList.hpp>
#include <kernel/kobject/KObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KDirectoryObject : KObjectT<KDirectoryObject, 1, TYPE_DIRECTORY> {
  using KObjectT::KObjectT;

  common::DoubleList<KObject> child_list;

  ~KDirectoryObject() override {
    while (!child_list.empty()) {
      KObject *obj = &child_list.front();
      child_list.pop_front();
      KE_OB_Release(obj);
    }
  }
};

template<>
struct ObjectTypeOf<TYPE_DIRECTORY> {
  using type = KDirectoryObject;
};
}// namespace kernel
