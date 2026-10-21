
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp>
[[__nodiscard__]] inline constexpr _NO_CFI _Tp *
addressof(_Tp &__x) noexcept {
  return __builtin_addressof(__x);
}

#if __has_feature(objc_arc)
// Objective-C++ Automatic Reference Counting uses qualified pointers
// that require special addressof() signatures.
template<class _Tp>
[[__nodiscard__]] inline __strong _Tp *addressof(__strong _Tp &__x) _NOEXCEPT {
  return &__x;
}

#if __has_feature(objc_arc_weak)
template<class _Tp>
[[__nodiscard__]] inline __weak _Tp *addressof(__weak _Tp &__x) _NOEXCEPT {
  return &__x;
}
#endif

template<class _Tp>
[[__nodiscard__]] inline __autoreleasing _Tp *addressof(__autoreleasing _Tp &__x) _NOEXCEPT {
  return &__x;
}

template<class _Tp>
[[__nodiscard__]] inline __unsafe_unretained _Tp *
addressof(__unsafe_unretained _Tp &__x) _NOEXCEPT {
  return &__x;
}
#endif

template<class _Tp>
_Tp *addressof(const _Tp &&) noexcept = delete;

}// namespace kstd
