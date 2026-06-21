
#pragma once

#include <__config.hpp>

namespace kstd {

#if !defined(_WORKAROUND_OBJCXX_COMPILER_INTRINSICS)
template<class _Tp>
struct add_pointer {
  using type = __add_pointer(_Tp);
};

#ifdef __GNUC__
template<class _Tp>
using __add_pointer_t = typename add_pointer<_Tp>::type;
#else
template<class _Tp>
using __add_pointer_t = __add_pointer(_Tp);
#endif

#else
template<class _Tp, bool = __is_referenceable_v<_Tp> || is_void<_Tp>::value>
struct __add_pointer_impl {
  using type = __libcpp_remove_reference_t<_Tp> *;
};
template<class _Tp>
struct __add_pointer_impl<_Tp, false> {
  using type = _Tp;
};

template<class _Tp>
using __add_pointer_t = typename __add_pointer_impl<_Tp>::type;

template<class _Tp>
struct add_pointer {
  using type = __add_pointer_t<_Tp>;
};

#endif

template<class _Tp>
using add_pointer_t = __add_pointer_t<_Tp>;

}// namespace kstd
