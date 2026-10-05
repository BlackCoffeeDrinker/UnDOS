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

  template<typename T, typename Func>
  void Each(Func &&func) {
    for (const auto obj: child_list) {
      if (obj->type == T::Type) {
        func(KObjectPtr{reinterpret_cast<T*>(obj)});
      }
    }
  }

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
