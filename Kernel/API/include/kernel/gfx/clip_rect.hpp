#pragma once

#include <stdint.h>

namespace kernel {

// Half-open clip rectangle in target (screen) coordinates.
struct KClipRect {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

}// namespace kernel
