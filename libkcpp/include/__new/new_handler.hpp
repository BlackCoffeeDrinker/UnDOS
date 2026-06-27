#pragma once

#include <__config.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>

namespace kstd {

template<class _Tp, class... _Args, class = decltype(::new (kstd::declval<void *>()) _Tp(kstd::declval<_Args>()...))>
constexpr _Tp *construct_at(_Tp *__location, _Args &&...__args) {
  static_assert(__location != nullptr, "null pointer given to construct_at");
  return ::new (static_cast<void *>(__location)) _Tp(kstd::forward<_Args>(__args)...);
}

}// namespace kstd
