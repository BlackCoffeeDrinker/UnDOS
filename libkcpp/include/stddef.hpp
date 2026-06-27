
#pragma once
#include <__config.hpp>

namespace kstd {
typedef __SIZE_TYPE__ size_t;
using ptrdiff_t = decltype(static_cast<int *>(nullptr) - static_cast<int *>(nullptr));

}// namespace kstd

using size_t = kstd::size_t;
using ptrdiff_t = kstd::ptrdiff_t;
using uint16_t = __UINT16_TYPE__;
