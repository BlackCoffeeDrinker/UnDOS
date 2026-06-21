
#pragma once
#include <__config.hpp>

namespace kstd {
#if __has_builtin(__remove_cv)
template<typename _Tp>
struct remove_cv {
  using type = __remove_cv(_Tp);
};
#else
template<typename _Tp>
struct remove_cv {
  using type = _Tp;
};

template<typename _Tp>
struct remove_cv<const _Tp> {
  using type = _Tp;
};

template<typename _Tp>
struct remove_cv<volatile _Tp> {
  using type = _Tp;
};

template<typename _Tp>
struct remove_cv<const volatile _Tp> {
  using type = _Tp;
};
#endif

template<typename _Tp>
using remove_cv_t = typename remove_cv<_Tp>::type;
}// namespace kstd
