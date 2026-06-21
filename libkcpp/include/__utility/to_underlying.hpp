
#pragma once
#include <__config.hpp>
#include <type_traits.hpp>

namespace kstd {

template<class _Tp>
using underlying_type_t = underlying_type_t<_Tp>;

template<class _Tp>
[[nodiscard]] constexpr underlying_type_t<_Tp> to_underlying(_Tp __val) noexcept {
  return static_cast<underlying_type_t<_Tp>>(__val);
}

}// namespace kstd
