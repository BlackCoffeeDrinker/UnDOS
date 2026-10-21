
#pragma once
#include <__config.hpp>

namespace kstd {
#if __has_builtin(__remove_reference_t)
template<class _Tp>
struct remove_reference {
  using type = __remove_reference_t(_Tp);
};

template<class _Tp>
using remove_reference_t = __remove_reference_t(_Tp);
#elif __has_builtin(__remove_reference)
template<class _Tp>
struct remove_reference {
  using type = __remove_reference(_Tp);
};

template<class _Tp>
using remove_reference_t = typename remove_reference<_Tp>::type;
#else
#error "remove_reference not implemented!"
#endif// __has_builtin(__remove_reference_t)
}// namespace kstd
