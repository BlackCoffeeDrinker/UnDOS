
#pragma once

#include <__config.hpp>

namespace kstd {

template<class... _Types>
struct __type_list {};

template<class>
struct __type_list_head;

template<class _Head, class... _Tail>
struct __type_list_head<__type_list<_Head, _Tail...>> {
  using type = _Head;
};

template<class _TypeList, size_t _Size, bool = _Size <= sizeof(typename __type_list_head<_TypeList>::type)>
struct __find_first;

template<class _Head, class... _Tail, size_t _Size>
struct __find_first<__type_list<_Head, _Tail...>, _Size, true> {
  using type = _Head;
};

template<class _Head, class... _Tail, size_t _Size>
struct __find_first<__type_list<_Head, _Tail...>, _Size, false> {
  using type = typename __find_first<__type_list<_Tail...>, _Size>::type;
};

}// namespace kstd
