
#pragma once
#include "../common/DoubleListLink.hpp"

namespace kernel::memory {

struct Storage : common::Link<Storage> {
  void *pointer;

  explicit Storage(void *ptr) : pointer(ptr) {}
};

}// namespace kernel::memory
