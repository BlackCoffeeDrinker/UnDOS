
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/decay.hpp>
#include <__type_traits/enable_if.hpp>
#include <__type_traits/integral_constant.hpp>
#include <__type_traits/is_base_of.hpp>
#include <__type_traits/is_core_convertible.hpp>
#include <__type_traits/is_member_pointer.hpp>
#include <__type_traits/is_reference_wrapper.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/is_void.hpp>
#include <__type_traits/nat.hpp>
#include <__type_traits/void_t.hpp>
#include <__utility/declval.hpp>
#include <__utility/forward.hpp>

namespace kstd {

#if __has_builtin(__builtin_invoke)

template<class, class... _Args>
struct __invoke_result_impl {};

template<class... _Args>
struct __invoke_result_impl<__void_t<decltype(__builtin_invoke(kstd::declval<_Args>()...))>, _Args...> {
  using type = decltype(__builtin_invoke(kstd::declval<_Args>()...));
};

template<class... _Args>
using __invoke_result = __invoke_result_impl<void, _Args...>;

template<class... _Args>
using __invoke_result_t = typename __invoke_result<_Args...>::type;

template<class... _Args>
constexpr __invoke_result_t<_Args...> __invoke(_Args &&...__args) noexcept(noexcept(__builtin_invoke(kstd::forward<_Args>(__args)...))) {
  return __builtin_invoke(kstd::forward<_Args>(__args)...);
}

template<class _Void, class... _Args>
inline const bool __is_invocable_impl = false;

template<class... _Args>
inline const bool __is_invocable_impl<__void_t<__invoke_result_t<_Args...>>, _Args...> = true;

template<class... _Args>
inline const bool __is_invocable_v = __is_invocable_impl<void, _Args...>;

template<class... _Args>
struct __is_invocable : integral_constant<bool, __is_invocable_v<_Args...>> {};

template<class _Ret, bool, class... _Args>
inline const bool __is_invocable_r_impl = false;

template<class _Ret, class... _Args>
inline const bool __is_invocable_r_impl<_Ret, true, _Args...> =
    __is_core_convertible<__invoke_result_t<_Args...>, _Ret>::value || is_void<_Ret>::value;

template<class _Ret, class... _Args>
inline const bool __is_invocable_r_v = __is_invocable_r_impl<_Ret, __is_invocable_v<_Args...>, _Args...>;

template<bool __is_invocable, class... _Args>
inline const bool __is_nothrow_invocable_impl = false;

template<class... _Args>
inline const bool __is_nothrow_invocable_impl<true, _Args...> = noexcept(__builtin_invoke(kstd::declval<_Args>()...));

template<class... _Args>
inline const bool __is_nothrow_invocable_v = __is_nothrow_invocable_impl<__is_invocable_v<_Args...>, _Args...>;

template<bool __is_invocable, class _Ret, class... _Args>
inline const bool __is_nothrow_invocable_r_impl = false;

template<class _Ret, class... _Args>
inline const bool __is_nothrow_invocable_r_impl<true, _Ret, _Args...> =
    __is_nothrow_core_convertible_v<__invoke_result_t<_Args...>, _Ret> || is_void<_Ret>::value;

template<class _Ret, class... _Args>
inline const bool __is_nothrow_invocable_r_v =
    __is_nothrow_invocable_r_impl<__is_nothrow_invocable_v<_Args...>, _Ret, _Args...>;

#else// __has_builtin(__builtin_invoke)

template<class _DecayedFp>
struct __member_pointer_class_type {};

template<class _Ret, class _ClassType>
struct __member_pointer_class_type<_Ret _ClassType::*> {
  typedef _ClassType type;
};

template<class _Fp,
         class _A0,
         class _DecayFp = decay_t<_Fp>,
         class _DecayA0 = decay_t<_A0>,
         class _ClassT = typename __member_pointer_class_type<_DecayFp>::type>
using __enable_if_bullet1 =
    enable_if_t<is_member_function_pointer<_DecayFp>::value &&
                (is_same<_ClassT, _DecayA0>::value || is_base_of<_ClassT, _DecayA0>::value)>;

template<class _Fp, class _A0, class _DecayFp = decay_t<_Fp>, class _DecayA0 = decay_t<_A0>>
using __enable_if_bullet2 =
    enable_if_t<is_member_function_pointer<_DecayFp>::value && __is_reference_wrapper<_DecayA0>::value>;

template<class _Fp,
         class _A0,
         class _DecayFp = decay_t<_Fp>,
         class _DecayA0 = decay_t<_A0>,
         class _ClassT = typename __member_pointer_class_type<_DecayFp>::type>
using __enable_if_bullet3 =
    enable_if_t<is_member_function_pointer<_DecayFp>::value &&
                !(is_same<_ClassT, _DecayA0>::value || is_base_of<_ClassT, _DecayA0>::value) &&
                !__is_reference_wrapper<_DecayA0>::value>;

template<class _Fp,
         class _A0,
         class _DecayFp = decay_t<_Fp>,
         class _DecayA0 = decay_t<_A0>,
         class _ClassT = typename __member_pointer_class_type<_DecayFp>::type>
using __enable_if_bullet4 =
    enable_if_t<is_member_object_pointer<_DecayFp>::value &&
                (is_same<_ClassT, _DecayA0>::value || is_base_of<_ClassT, _DecayA0>::value)>;

template<class _Fp, class _A0, class _DecayFp = decay_t<_Fp>, class _DecayA0 = decay_t<_A0>>
using __enable_if_bullet5 =
    enable_if_t<is_member_object_pointer<_DecayFp>::value && __is_reference_wrapper<_DecayA0>::value>;

template<class _Fp,
         class _A0,
         class _DecayFp = decay_t<_Fp>,
         class _DecayA0 = decay_t<_A0>,
         class _ClassT = typename __member_pointer_class_type<_DecayFp>::type>
using __enable_if_bullet6 =
    enable_if_t<is_member_object_pointer<_DecayFp>::value &&
                !(is_same<_ClassT, _DecayA0>::value || is_base_of<_ClassT, _DecayA0>::value) &&
                !__is_reference_wrapper<_DecayA0>::value>;

// __invoke forward declarations

// fall back - none of the bullets

template<class... _Args>
__nat __invoke(_Args &&...__args);

// bullets 1, 2 and 3

// clang-format off
template <class _Fp, class _A0, class... _Args, class = __enable_if_bullet1<_Fp, _A0> >
inline  constexpr
decltype((kstd::declval<_A0>().*kstd::declval<_Fp>())(kstd::declval<_Args>()...))
__invoke(_Fp&& __f, _A0&& __a0, _Args&&... __args)
    noexcept(noexcept((static_cast<_A0&&>(__a0).*__f)(static_cast<_Args&&>(__args)...)))
               { return (static_cast<_A0&&>(__a0).*__f)(static_cast<_Args&&>(__args)...); }

template <class _Fp, class _A0, class... _Args, class = __enable_if_bullet2<_Fp, _A0> >
inline  constexpr
decltype((kstd::declval<_A0>().get().*kstd::declval<_Fp>())(kstd::declval<_Args>()...))
__invoke(_Fp&& __f, _A0&& __a0, _Args&&... __args)
    noexcept(noexcept((__a0.get().*__f)(static_cast<_Args&&>(__args)...)))
               { return (__a0.get().*__f)(static_cast<_Args&&>(__args)...); }

template <class _Fp, class _A0, class... _Args, class = __enable_if_bullet3<_Fp, _A0> >
inline  constexpr
decltype(((*kstd::declval<_A0>()).*kstd::declval<_Fp>())(kstd::declval<_Args>()...))
__invoke(_Fp&& __f, _A0&& __a0, _Args&&... __args)
    noexcept(noexcept(((*static_cast<_A0&&>(__a0)).*__f)(static_cast<_Args&&>(__args)...)))
               { return ((*static_cast<_A0&&>(__a0)).*__f)(static_cast<_Args&&>(__args)...); }

// bullets 4, 5 and 6

template <class _Fp, class _A0, class = __enable_if_bullet4<_Fp, _A0> >
inline  constexpr
decltype(kstd::declval<_A0>().*kstd::declval<_Fp>())
__invoke(_Fp&& __f, _A0&& __a0)
    noexcept(noexcept(static_cast<_A0&&>(__a0).*__f))
               { return static_cast<_A0&&>(__a0).*__f; }

template <class _Fp, class _A0, class = __enable_if_bullet5<_Fp, _A0> >
inline  constexpr
decltype(kstd::declval<_A0>().get().*kstd::declval<_Fp>())
__invoke(_Fp&& __f, _A0&& __a0)
    noexcept(noexcept(__a0.get().*__f))
               { return __a0.get().*__f; }

template <class _Fp, class _A0, class = __enable_if_bullet6<_Fp, _A0> >
inline  constexpr
decltype((*kstd::declval<_A0>()).*kstd::declval<_Fp>())
__invoke(_Fp&& __f, _A0&& __a0)
    noexcept(noexcept((*static_cast<_A0&&>(__a0)).*__f))
               { return (*static_cast<_A0&&>(__a0)).*__f; }

// bullet 7

template <class _Fp, class... _Args>
inline  constexpr
decltype(kstd::declval<_Fp>()(kstd::declval<_Args>()...))
__invoke(_Fp&& __f, _Args&&... __args)
    noexcept(noexcept(static_cast<_Fp&&>(__f)(static_cast<_Args&&>(__args)...)))
               { return static_cast<_Fp&&>(__f)(static_cast<_Args&&>(__args)...); }
// clang-format on

// __invokable
template<class _Ret, class _Fp, class... _Args>
struct __invokable_r {
  template<class _XFp, class... _XArgs>
  static decltype(kstd::__invoke(kstd::declval<_XFp>(), kstd::declval<_XArgs>()...)) __try_call(int);
  template<class _XFp, class... _XArgs>
  static __nat __try_call(...);

  // FIXME: Check that _Ret, _Fp, and _Args... are all complete types, cv void,
  // or incomplete array types as required by the standard.
  using _Result = decltype(__try_call<_Fp, _Args...>(0));

  using type = __conditional_t<_IsNotSame<_Result, __nat>::value,
                               __conditional_t<is_void<_Ret>::value, true_type, __is_core_convertible<_Result, _Ret>>,
                               false_type>;
  static const bool value = type::value;
};
template<class _Fp, class... _Args>
using __is_invocable = __invokable_r<void, _Fp, _Args...>;

template<bool _IsInvokable, bool _IsCVVoid, class _Ret, class _Fp, class... _Args>
struct __nothrow_invokable_r_imp {
  static const bool value = false;
};

template<class _Ret, class _Fp, class... _Args>
struct __nothrow_invokable_r_imp<true, false, _Ret, _Fp, _Args...> {
  typedef __nothrow_invokable_r_imp _ThisT;

  template<class _Tp>
  static void __testnoexcept(_Tp) noexcept;

  static const bool value =
      noexcept(_ThisT::__testnoexcept<_Ret>(kstd::__invoke(kstd::declval<_Fp>(), kstd::declval<_Args>()...)));
};

template<class _Ret, class _Fp, class... _Args>
struct __nothrow_invokable_r_imp<true, true, _Ret, _Fp, _Args...> {
#ifdef _LIBCPP_CXX03_LANG
  static const bool value = false;
#else
  static const bool value = noexcept(kstd::__invoke(kstd::declval<_Fp>(), kstd::declval<_Args>()...));
#endif
};

template<class _Ret, class _Fp, class... _Args>
using __nothrow_invokable_r =
    __nothrow_invokable_r_imp<__invokable_r<_Ret, _Fp, _Args...>::value, is_void<_Ret>::value, _Ret, _Fp, _Args...>;

template<class _Fp, class... _Args>
using __nothrow_invokable =
    __nothrow_invokable_r_imp<__is_invocable<_Fp, _Args...>::value, true, void, _Fp, _Args...>;

template<class _Func, class... _Args>
inline const bool __is_invocable_v = __is_invocable<_Func, _Args...>::value;

template<class _Ret, class _Func, class... _Args>
inline const bool __is_invocable_r_v = __invokable_r<_Ret, _Func, _Args...>::value;

template<class _Func, class... _Args>
inline const bool __is_nothrow_invocable_v = __nothrow_invokable<_Func, _Args...>::value;

template<class _Ret, class _Func, class... _Args>
inline const bool __is_nothrow_invocable_r_v = __nothrow_invokable_r<_Ret, _Func, _Args...>::value;

template<class _Func, class... _Args>
struct __invoke_result
    : enable_if<__is_invocable_v<_Func, _Args...>, typename __invokable_r<void, _Func, _Args...>::_Result> {};

template<class _Func, class... _Args>
using __invoke_result_t = typename __invoke_result<_Func, _Args...>::type;

#endif// __has_builtin(__builtin_invoke_r)

template<class _Ret, class _Func, class... _Args>
struct __is_invocable_r : integral_constant<bool, __is_invocable_r_v<_Ret, _Func, _Args...>> {};

template<class _Ret, bool = is_void<_Ret>::value>
struct __invoke_void_return_wrapper {
  template<class... _Args>
  constexpr static _Ret __call(_Args &&...__args) {
    return kstd::__invoke(kstd::forward<_Args>(__args)...);
  }
};

template<class _Ret>
struct __invoke_void_return_wrapper<_Ret, true> {
  template<class... _Args>
  constexpr static void __call(_Args &&...__args) {
    kstd::__invoke(kstd::forward<_Args>(__args)...);
  }
};

template<class _Ret, class... _Args>
constexpr _Ret __invoke_r(_Args &&...__args) {
  return __invoke_void_return_wrapper<_Ret>::__call(kstd::forward<_Args>(__args)...);
}

// is_invocable

template<class _Fn, class... _Args>
struct is_invocable : bool_constant<__is_invocable_v<_Fn, _Args...>> {};

template<class _Ret, class _Fn, class... _Args>
struct is_invocable_r : bool_constant<__is_invocable_r_v<_Ret, _Fn, _Args...>> {};

template<class _Fn, class... _Args>
inline constexpr bool is_invocable_v = __is_invocable_v<_Fn, _Args...>;

template<class _Ret, class _Fn, class... _Args>
inline constexpr bool is_invocable_r_v = __is_invocable_r_v<_Ret, _Fn, _Args...>;

// is_nothrow_invocable

template<class _Fn, class... _Args>
struct is_nothrow_invocable : bool_constant<__is_nothrow_invocable_v<_Fn, _Args...>> {};

template<class _Ret, class _Fn, class... _Args>
struct is_nothrow_invocable_r
    : bool_constant<__is_nothrow_invocable_r_v<_Ret, _Fn, _Args...>> {};

template<class _Fn, class... _Args>
inline constexpr bool is_nothrow_invocable_v = __is_nothrow_invocable_v<_Fn, _Args...>;

template<class _Ret, class _Fn, class... _Args>
inline constexpr bool is_nothrow_invocable_r_v =
    __is_nothrow_invocable_r_v<_Ret, _Fn, _Args...>;

template<class _Fn, class... _Args>
struct invoke_result : __invoke_result<_Fn, _Args...> {};

template<class _Fn, class... _Args>
using invoke_result_t = __invoke_result_t<_Fn, _Args...>;
}// namespace kstd
