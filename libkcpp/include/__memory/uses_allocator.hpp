
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp>
struct __has_allocator_type {
  private:
  template<class _Up>
  static false_type __test(...);
  template<class _Up>
  static true_type __test(typename _Up::allocator_type * = 0);

  public:
  static const bool value = decltype(__test<_Tp>(0))::value;
};

template<class _Tp, class _Alloc, bool = __has_allocator_type<_Tp>::value>
struct __uses_allocator : public integral_constant<bool, is_convertible<_Alloc, typename _Tp::allocator_type>::value> {
};

template<class _Tp, class _Alloc>
struct __uses_allocator<_Tp, _Alloc, false> : public false_type {};

template<class _Tp, class _Alloc>
struct uses_allocator : public __uses_allocator<_Tp, _Alloc> {};

template<class _Tp, class _Alloc>
inline constexpr bool uses_allocator_v = uses_allocator<_Tp, _Alloc>::value;

}// namespace kstd
