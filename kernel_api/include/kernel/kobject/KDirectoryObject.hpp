#pragma once

#include <kernel/__core.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <kernel/kobject/KObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KDirectoryObject : KObjectT<KDirectoryObject, 1, TYPE_DIRECTORY> {
  adt::AvlTree<KObject, &KObject::node> children;

  ~KDirectoryObject() override {
    children.clear([](KObject *obj) {
      if (obj) KE_OB_Release(obj);
    });
  }
};

template<>
struct ObjectTypeOf<TYPE_DIRECTORY> {
  using type = KDirectoryObject;
};
}// namespace kernel
