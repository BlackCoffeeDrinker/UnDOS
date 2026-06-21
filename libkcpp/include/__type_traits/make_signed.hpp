
#pragma once

#include <__config.hpp>
#include <__type_traits/copy_cv.hpp>
#include <__type_traits/is_enum.hpp>
#include <__type_traits/is_integral.hpp>
#include <__type_traits/remove_cv.hpp>
#include <__type_traits/type_list.hpp>

namespace kstd {

#if __has_builtin(__make_signed)
template<class _Tp>
using __make_signed_t = __make_signed(_Tp);

#else
using __signed_types =
    __type_list<signed char,
                signed short,
                signed int,
                signed long,
                signed long long
#if _HAS_INT128
                ,
                __int128_t
#endif
                >;

template<class _Tp, bool = is_integral<_Tp>::value || is_enum<_Tp>::value>
struct __make_signed{};

template<class _Tp>
struct __make_signed<_Tp, true> {
  typedef typename __find_first<__signed_types, sizeof(_Tp)>::type type;
};

// clang-format off
template <> struct __make_signed<bool,               true> {};
template <> struct __make_signed<  signed short,     true> {typedef short     type;};
template <> struct __make_signed<unsigned short,     true> {typedef short     type;};
template <> struct __make_signed<  signed int,       true> {typedef int       type;};
template <> struct __make_signed<unsigned int,       true> {typedef int       type;};
template <> struct __make_signed<  signed long,      true> {typedef long      type;};
template <> struct __make_signed<unsigned long,      true> {typedef long      type;};
template <> struct __make_signed<  signed long long, true> {typedef long long type;};
template <> struct __make_signed<unsigned long long, true> {typedef long long type;};
#  if _HAS_INT128
template <> struct __make_signed<__int128_t,         true> {typedef __int128_t type;};
template <> struct __make_signed<__uint128_t,        true> {typedef __int128_t type;};
#  endif
// clang-format on

template<class _Tp>
using __make_signed_t = __copy_cv_t<_Tp, typename __make_signed<remove_cv_t<_Tp>>::type>;

#endif// __has_builtin(__make_signed)

template<class _Tp>
struct make_signed {
  using type = __make_signed_t<_Tp>;
};

template<class _Tp>
using make_signed_t = __make_signed_t<_Tp>;

}// namespace kstd
