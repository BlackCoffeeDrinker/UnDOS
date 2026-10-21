
#pragma once

#include <__config.hpp>
#include <__fwd/functional.hpp>
#include <__type_traits/decay.hpp>

namespace kstd {

template<class _Tp>
struct __unwrap_reference {
  using type = _Tp;
};

template<class _Tp>
struct __unwrap_reference<reference_wrapper<_Tp>> {
  using type = _Tp &;
};

template<class _Tp>
using __unwrap_ref_decay_t = typename __unwrap_reference<decay_t<_Tp>>::type;

template<class _Tp>
struct unwrap_reference : __unwrap_reference<_Tp> {};

template<class _Tp>
using unwrap_reference_t = typename unwrap_reference<_Tp>::type;

template<class _Tp>
struct unwrap_ref_decay : unwrap_reference<decay_t<_Tp>> {};

template<class _Tp>
using unwrap_ref_decay_t = __unwrap_ref_decay_t<_Tp>;

}// namespace kstd
