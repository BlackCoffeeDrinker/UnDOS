
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp>
struct remove_extent {
  using type = __remove_extent(_Tp);
};

#ifdef __GNUC__
template<class _Tp>
using __remove_extent_t = typename remove_extent<_Tp>::type;
#else
template<class _Tp>
using __remove_extent_t = __remove_extent(_Tp);
#endif

template<class _Tp>
using remove_extent_t = __remove_extent_t<_Tp>;


}// namespace kstd
