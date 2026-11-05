
#pragma once

#include <__config.hpp>

namespace kstd {
struct __nat {
  __nat() = delete;
  __nat(const __nat &) = delete;
  __nat &operator=(const __nat &) = delete;
  ~__nat() = delete;
};

}// namespace kstd
