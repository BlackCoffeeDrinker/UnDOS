
#pragma once

#include <__config.hpp>

namespace kstd {

template<size_t _Ip, class _Tp>
struct tuple_element;

template<size_t _Ip, class _Tp>
struct tuple_element<_Ip, const _Tp> {
  using type = const typename tuple_element<_Ip, _Tp>::type;
};

template<size_t _Ip, class _Tp>
struct tuple_element<_Ip, volatile _Tp> {
  using type = volatile typename tuple_element<_Ip, _Tp>::type;
};

template<size_t _Ip, class _Tp>
struct tuple_element<_Ip, const volatile _Tp> {
  using type = const volatile typename tuple_element<_Ip, _Tp>::type;
};

template<size_t _Ip, class... _Tp>
using tuple_element_t = typename tuple_element<_Ip, _Tp...>::type;

}// namespace kstd
