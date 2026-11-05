
#pragma once

#include <__config.hpp>

namespace kstd {

struct piecewise_construct_t {
  explicit piecewise_construct_t() = default;
};

inline constexpr piecewise_construct_t piecewise_construct = piecewise_construct_t();

}// namespace kstd
