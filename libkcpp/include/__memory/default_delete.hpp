
#pragma once

#include <__config.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/is_array.hpp>
#include <__type_traits/is_convertible.hpp>
#include <__type_traits/is_void.hpp>

namespace kstd {

template <class _Tp>
struct default_delete {
  constexpr default_delete() noexcept = default;

  template <class _Up, enable_if_t<is_convertible<_Up*, _Tp*>::value, int> = 0>
  constexpr default_delete(const default_delete<_Up>&) noexcept {}

  void operator()(_Tp* __ptr) const noexcept {
    static_assert(sizeof(_Tp) > 0, "default_delete can not delete incomplete type");
    static_assert(!is_void<_Tp>::value, "default_delete can not delete incomplete type");
    delete __ptr;
  }
};

template <class _Tp>
struct default_delete<_Tp[]> {
  constexpr default_delete() noexcept = default;

  template <class _Up, enable_if_t<is_convertible<_Up(*)[], _Tp(*)[]>::value, int> = 0>
  constexpr default_delete(const default_delete<_Up[]>&) noexcept {}

  void operator()(_Tp* __ptr) const noexcept {
    static_assert(sizeof(_Tp) > 0, "default_delete can not delete incomplete type");
    delete[] __ptr;
  }
};

} // namespace kstd
