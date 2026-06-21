
#pragma once
#include <__config.hpp>

#include "stddef.hpp"

namespace kstd {
template<size_t s>
struct buffer_string {

  private:
  char _data[s];
  size_t _len;
};
}// namespace kstd
