
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/decay.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/remove_cvref.hpp>
#include <__type_traits/type_identity.hpp>
#include <__type_traits/void_t.hpp>
#include <__utility/declval.hpp>
#include <__utility/empty.hpp>

namespace kstd {

#if __has_builtin(__builtin_common_type)

template<class... _Args>
struct common_type;

template<class... _Args>
using __common_type_t = typename common_type<_Args...>::type;

template<class... _Args>
struct common_type : __builtin_common_type<__common_type_t, __type_identity, __empty, _Args...> {};

#else
// Let COND_RES(X, Y) be:
template<class _Tp, class _Up>
using __cond_type = decltype(false ? kstd::declval<_Tp>() : kstd::declval<_Up>());

template<class _Tp, class _Up, class = void>
struct __common_type3 {};

// sub-bullet 4 - "if COND_RES(CREF(D1), CREF(D2)) denotes a type..."
template<class _Tp, class _Up>
struct __common_type3<_Tp, _Up, void_t<__cond_type<const _Tp &, const _Up &>>> {
  using type = remove_cvref_t<__cond_type<const _Tp &, const _Up &>>;
};

template<class _Tp, class _Up, class = void>
struct __common_type2_imp : __common_type3<_Tp, _Up> {};


// sub-bullet 3 - "if decay_t<decltype(false ? declval<D1>() : declval<D2>())> ..."
template<class _Tp, class _Up>
struct __common_type2_imp<_Tp, _Up, __void_t<decltype(true ? kstd::declval<_Tp>() : kstd::declval<_Up>())>> {
  using type = decay_t<decltype(true ? kstd::declval<_Tp>() : kstd::declval<_Up>())>;
};

template<class, class = void>
struct __common_type_impl {};

template<class... _Tp>
struct __common_types;
template<class... _Tp>
struct common_type;

template<class _Tp, class _Up>
struct __common_type_impl<__common_types<_Tp, _Up>, __void_t<typename common_type<_Tp, _Up>::type>> {
  typedef typename common_type<_Tp, _Up>::type type;
};

template<class _Tp, class _Up, class _Vp, class... _Rest>
struct __common_type_impl<__common_types<_Tp, _Up, _Vp, _Rest...>, __void_t<typename common_type<_Tp, _Up>::type>>
    : __common_type_impl<__common_types<typename common_type<_Tp, _Up>::type, _Vp, _Rest...>> {};

// bullet 1 - sizeof...(Tp) == 0

template<>
struct common_type<> {};

// bullet 2 - sizeof...(Tp) == 1

template<class _Tp>
struct common_type<_Tp> : public common_type<_Tp, _Tp> {};

// bullet 3 - sizeof...(Tp) == 2

// sub-bullet 1 - "If is_same_v<T1, D1> is false or ..."
template<class _Tp, class _Up>
struct common_type<_Tp, _Up>
    : __conditional_t<is_same<_Tp, decay_t<_Tp>>::value && is_same<_Up, decay_t<_Up>>::value,
                      __common_type2_imp<_Tp, _Up>,
                      common_type<decay_t<_Tp>, decay_t<_Up>>> {};

// bullet 4 - sizeof...(Tp) > 2

template<class _Tp, class _Up, class _Vp, class... _Rest>
struct common_type<_Tp, _Up, _Vp, _Rest...> : __common_type_impl<__common_types<_Tp, _Up, _Vp, _Rest...>> {};

#endif

template<class... _Tp>
using common_type_t = typename common_type<_Tp...>::type;

}// namespace kstd
