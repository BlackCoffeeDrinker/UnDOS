
#pragma once
#include <__config.hpp>

namespace kstd {
template<class _Tp>
struct __type_identity {
  typedef _Tp type;
};

template<class _Tp>
using __type_identity_t = typename __type_identity<_Tp>::type;

template<class _Tp>
struct type_identity {
  typedef _Tp type;
};
template<class _Tp>
using type_identity_t = typename type_identity<_Tp>::type;

}// namespace kstd
