
#pragma once

#include <__concepts/same_as.hpp>
#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/is_array.hpp>
#include <__type_traits/is_object.hpp>
#include <__type_traits/is_primary_template.hpp>
#include <__type_traits/remove_cv.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__type_traits/remove_extent.hpp>

namespace kstd {

// [readable.traits]
template<class>
struct __cond_value_type {};

template<class _Tp>
  requires is_object_v<_Tp>
struct __cond_value_type<_Tp> {
  using value_type = remove_cv_t<_Tp>;
};

template<class _Tp>
concept __has_member_value_type = requires { typename _Tp::value_type; };

template<class _Tp>
concept __has_member_element_type = requires { typename _Tp::element_type; };

template<class>
struct indirectly_readable_traits {};

template<class _Ip>
  requires is_array_v<_Ip>
struct indirectly_readable_traits<_Ip> {
  using value_type = remove_cv_t<remove_extent_t<_Ip>>;
};

template<class _Ip>
struct indirectly_readable_traits<const _Ip> : indirectly_readable_traits<_Ip> {};

template<class _Tp>
struct indirectly_readable_traits<_Tp *> : __cond_value_type<_Tp> {};

template<__has_member_value_type _Tp>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::value_type> {};

template<__has_member_element_type _Tp>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::element_type> {};

template<__has_member_value_type _Tp>
  requires __has_member_element_type<_Tp>
struct indirectly_readable_traits<_Tp> {};

template<__has_member_value_type _Tp>
  requires __has_member_element_type<_Tp> &&
           same_as<remove_cv_t<typename _Tp::element_type>, remove_cv_t<typename _Tp::value_type>>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::value_type> {};

}// namespace kstd
