
#pragma once

#include <__config.hpp>
namespace kstd {

template<size_t, class>
struct tuple_element;

template<size_t _Np, class _Tp>
using __tuple_element_t = typename tuple_element<_Np, _Tp>::type;


template<class...>
class tuple;

template<class>
inline const bool __is_tuple_v = false;

template<class... _Tp>
inline const bool __is_tuple_v<tuple<_Tp...>> = true;

template<size_t _Ip, class... _Tp>
struct tuple_element<_Ip, tuple<_Tp...>> {
  using type = __type_pack_element<_Ip, _Tp...>;
};

template<class>
struct tuple_size;

template<size_t _Ip, class... _Tp>
constexpr typename tuple_element<_Ip, tuple<_Tp...>>::type &
get(tuple<_Tp...> &) noexcept;

template<size_t _Ip, class... _Tp>
constexpr const typename tuple_element<_Ip, tuple<_Tp...>>::type &
get(const tuple<_Tp...> &) noexcept;

template<size_t _Ip, class... _Tp>
constexpr typename tuple_element<_Ip, tuple<_Tp...>>::type &&
get(tuple<_Tp...> &&) noexcept;

template<size_t _Ip, class... _Tp>
constexpr const typename tuple_element<_Ip, tuple<_Tp...>>::type &&
get(const tuple<_Tp...> &&) noexcept;


}// namespace kstd
