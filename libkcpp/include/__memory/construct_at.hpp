
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp, class... _Args, class = decltype(::new (kstd::declval<void *>()) _Tp(kstd::declval<_Args>()...))>
_KSTD_API constexpr _Tp *construct_at(_Tp *__location, _Args &&...__args) {
  static_assert(__location != nullptr, "null pointer given to construct_at");
  return ::new (static_cast<void *>(__location)) _Tp(kstd::forward<_Args>(__args)...);
}

template<class _Tp, class... _Args, class = decltype(::new (kstd::declval<void *>()) _Tp(kstd::declval<_Args>()...))>
_KSTD_API constexpr _Tp *__construct_at(_Tp *__location, _Args &&...__args) {
  return kstd::construct_at(__location, kstd::forward<_Args>(__args)...);
}


template<class _Tp>
_KSTD_API constexpr void __destroy_at(_Tp *__loc) {
  if constexpr (is_array_v<_Tp>) {
    for (auto &&__val: *__loc)
      kstd::__destroy_at(kstd::addressof(__val));
  } else {
    __loc->~_Tp();
  }
}

template<class _Tp>
_KSTD_API constexpr void destroy_at(_Tp *__loc) {
  kstd::__destroy_at(__loc);
}

}// namespace kstd
