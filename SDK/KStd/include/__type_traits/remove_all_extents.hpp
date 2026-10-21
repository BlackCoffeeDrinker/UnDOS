
#pragma once

#include <__config.hpp>

namespace kstd {
template<class _Tp>
struct remove_all_extents {
  using type = __remove_all_extents(_Tp);
};

#ifdef _LIBCPP_COMPILER_GCC
template<class _Tp>
using __remove_all_extents_t = typename remove_all_extents<_Tp>::type;
#else
template<class _Tp>
using __remove_all_extents_t = __remove_all_extents(_Tp);
#endif

template<class _Tp>
using remove_all_extents_t = __remove_all_extents_t<_Tp>;

}// namespace kstd
