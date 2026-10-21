
#pragma once

#include <__config.hpp>

namespace kstd {

#if __has_builtin(__remove_const)
template<class _Tp>
struct remove_const {
  using type = __remove_const(_Tp);
};

template<class _Tp>
using __remove_const_t = __remove_const(_Tp);
#else
template<class _Tp>
struct remove_const {
  typedef _Tp type;
};
template<class _Tp>
struct remove_const<const _Tp> {
  typedef _Tp type;
};

template<class _Tp>
using __remove_const_t = typename remove_const<_Tp>::type;
#endif// __has_builtin(__remove_const)

template<class _Tp>
using remove_const_t = __remove_const_t<_Tp>;

}// namespace kstd
