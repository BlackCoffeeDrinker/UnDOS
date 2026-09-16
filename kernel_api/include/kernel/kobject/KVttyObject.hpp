#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/tty/tty.hpp>

namespace kernel {

struct KVttySourceObject : KObjectT<KVttySourceObject, 1, TYPE_TTY_SOURCE> {
  using KObjectT::KObjectT;
  tty::VttyPtr vtty;

  ~KVttySourceObject() override {
    if (vtty) {
      vtty->close_source();
    }
  }
};

struct KVttySinkObject : KObjectT<KVttySinkObject, 1, TYPE_TTY_SINK> {
  using KObjectT::KObjectT;
  tty::VttyPtr vtty;

  ~KVttySinkObject() override {
    if (vtty) {
      vtty->close_sink();
    }
  }
};

template<>
struct ObjectTypeOf<TYPE_TTY_SOURCE> {
  using type = KVttySourceObject;
};

template<>
struct ObjectTypeOf<TYPE_TTY_SINK> {
  using type = KVttySinkObject;
};

} // namespace kernel
