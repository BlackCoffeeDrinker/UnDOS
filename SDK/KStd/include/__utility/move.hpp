
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/is_constructible.hpp>
#include <__type_traits/is_nothrow_constructible.hpp>
#include <__type_traits/remove_reference.hpp>

namespace kstd {
template<class _Tp>
[[__nodiscard__]] inline constexpr remove_reference_t<_Tp> &&
move(_Tp &&__t) noexcept {
  using _Up = remove_reference_t<_Tp>;
  return static_cast<_Up &&>(__t);
}

template<class _Tp>
using __move_if_noexcept_result_t =
    __conditional_t<!is_nothrow_move_constructible<_Tp>::value && is_copy_constructible<_Tp>::value, const _Tp &, _Tp &&>;

template<class _Tp>
[[__nodiscard__]] inline constexpr __move_if_noexcept_result_t<_Tp>
move_if_noexcept(_Tp &__x) noexcept {
  return kstd::move(__x);
}

}// namespace kstd
