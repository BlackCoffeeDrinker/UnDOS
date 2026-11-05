
#pragma once
#include <__config.hpp>

namespace kstd {
template<class _Tp>
struct add_lvalue_reference {
  using type = __add_lvalue_reference(_Tp);
};

#ifdef __GNUC__
template<class _Tp>
using __add_lvalue_reference_t = typename add_lvalue_reference<_Tp>::type;
#else
template<class _Tp>
using __add_lvalue_reference_t = __add_lvalue_reference(_Tp);
#endif

template<class _Tp>
using add_lvalue_reference_t = __add_lvalue_reference_t<_Tp>;

template<class _Tp>
struct add_rvalue_reference {
  using type = __add_rvalue_reference(_Tp);
};

#ifdef __GNUC__
template<class _Tp>
using __add_rvalue_reference_t = typename add_rvalue_reference<_Tp>::type;
#else
template<class _Tp>
using __add_rvalue_reference_t = __add_rvalue_reference(_Tp);
#endif

template<class _Tp>
using add_rvalue_reference_t = __add_rvalue_reference_t<_Tp>;

}// namespace kstd
