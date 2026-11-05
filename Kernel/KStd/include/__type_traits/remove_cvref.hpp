
#pragma once
#include <__config.hpp>

namespace kstd {

#if defined(__GNUC__)
template<class _Tp>
struct __remove_cvref_gcc {
  using type = __remove_cvref(_Tp);
};

template<class _Tp>
using __remove_cvref_t = typename __remove_cvref_gcc<_Tp>::type;
#else
template<class _Tp>
using __remove_cvref_t = __remove_cvref(_Tp);
#endif// __has_builtin(__remove_cvref)

template<class _Tp>
struct remove_cvref {
  using type = __remove_cvref(_Tp);
};

template<class _Tp>
using remove_cvref_t = __remove_cvref_t<_Tp>;
}// namespace kstd
