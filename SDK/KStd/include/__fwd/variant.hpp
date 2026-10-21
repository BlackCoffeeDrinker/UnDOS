
#pragma once

#include <__config.hpp>

namespace kstd {
template<class... _Types>
class variant;

template<class _Tp>
struct variant_size;

template<class _Tp>
inline constexpr size_t variant_size_v = variant_size<_Tp>::value;

template<size_t _Ip, class _Tp>
struct variant_alternative;

template<size_t _Ip, class _Tp>
using variant_alternative_t = typename variant_alternative<_Ip, _Tp>::type;

inline constexpr size_t variant_npos = static_cast<size_t>(-1);

template<size_t _Ip, class... _Types>
constexpr variant_alternative_t<_Ip, variant<_Types...>> &get(variant<_Types...> &);

template<size_t _Ip, class... _Types>
constexpr variant_alternative_t<_Ip, variant<_Types...>> &&get(variant<_Types...> &&);

template<size_t _Ip, class... _Types>
constexpr const variant_alternative_t<_Ip, variant<_Types...>> &get(const variant<_Types...> &);

template<size_t _Ip, class... _Types>
constexpr const variant_alternative_t<_Ip, variant<_Types...>> &&get(const variant<_Types...> &&);

template<class _Tp, class... _Types>
constexpr _Tp &get(variant<_Types...> &);

template<class _Tp, class... _Types>
constexpr _Tp &&get(variant<_Types...> &&);

template<class _Tp, class... _Types>
constexpr const _Tp &get(const variant<_Types...> &);

template<class _Tp, class... _Types>
constexpr const _Tp &&get(const variant<_Types...> &&);

}// namespace kstd
