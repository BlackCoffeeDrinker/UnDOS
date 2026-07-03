#pragma once

#include <kernel/adt/avl_tree.hpp>
#include <kernel/kobject/KObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

struct KDirectoryObject : KObjectT<KDirectoryObject, 1, TYPE_DIRECTORY> {
  adt::AvlTree<KObject, &KObject::node> children;

  ~KDirectoryObject() override {
    children.clear([](KObject *obj) {
      if (obj) obj->release();
    });
  }
};

} // namespace kernel
