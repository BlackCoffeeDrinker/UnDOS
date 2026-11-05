
#pragma once

#include <__config.hpp>
#include <__functional/binary_function.hpp>
#include <__functional/unary_function.hpp>
#include <__fwd/functional.hpp>
#include <__type_traits/desugars_to.hpp>
#include <__type_traits/is_generic_transparent_comparator.hpp>
#include <__type_traits/is_integral.hpp>
#include <__type_traits/make_transparent.hpp>
#include <__utility/forward.hpp>

namespace kstd {
template<class _Tp = void>
struct plus : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x + __y;
  }
};

_CTAD_SUPPORTED_FOR_TYPE(plus);

// The non-transparent kstd::plus specialization is only equivalent to a raw plus
// operator when we don't perform an implicit conversion when calling it.
template<class _Tp>
inline const bool __desugars_to_v<__plus_tag, plus<_Tp>, _Tp, _Tp> = true;

template<class _Tp, class _Up>
inline const bool __desugars_to_v<__plus_tag, plus<void>, _Tp, _Up> = true;

template<>
struct plus<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) + kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) + kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) + kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct minus : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x - __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(minus);

template<>
struct minus<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) - kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) - kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) - kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct multiplies : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x * __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(multiplies);

template<>
struct multiplies<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) * kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) * kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) * kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct divides : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x / __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(divides);

template<>
struct divides<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) / kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) / kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) / kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct modulus : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x % __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(modulus);

template<>
struct modulus<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) % kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) % kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) % kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct negate : __unary_function<_Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x) const { return -__x; }
};
_CTAD_SUPPORTED_FOR_TYPE(negate);

template<>
struct negate<void> {
  template<class _Tp>
  constexpr auto operator()(_Tp &&__x) const
      noexcept(noexcept(-kstd::forward<_Tp>(__x)))//
      -> decltype(-kstd::forward<_Tp>(__x)) {
    return -kstd::forward<_Tp>(__x);
  }
  typedef void is_transparent;
};

// Bitwise operations

template<class _Tp = void>
struct bit_and : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x & __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(bit_and);

template<>
struct bit_and<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) &
                        kstd::forward<_T2>(__u))) -> decltype(kstd::forward<_T1>(__t) & kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) & kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct bit_not : __unary_function<_Tp, _Tp> {
  constexpr _Tp operator()(const _Tp &__x) const { return ~__x; }
};
_CTAD_SUPPORTED_FOR_TYPE(bit_not);

template<>
struct bit_not<void> {
  template<class _Tp>
  constexpr auto operator()(_Tp &&__x) const
      noexcept(noexcept(~kstd::forward<_Tp>(__x)))//
      -> decltype(~kstd::forward<_Tp>(__x)) {
    return ~kstd::forward<_Tp>(__x);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct bit_or : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x | __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(bit_or);

template<>
struct bit_or<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) | kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) | kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) | kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct bit_xor : __binary_function<_Tp, _Tp, _Tp> {
  typedef _Tp __result_type;// used by valarray
  constexpr _Tp operator()(const _Tp &__x, const _Tp &__y) const {
    return __x ^ __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(bit_xor);

template<>
struct bit_xor<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) ^ kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) ^ kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) ^ kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

// Comparison operations

template<class _Tp = void>
struct equal_to : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x == __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(equal_to);

template<>
struct equal_to<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) == kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) == kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) == kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

// The non-transparent kstd::equal_to specialization is only equivalent to a raw equality
// comparison when we don't perform an implicit conversion when calling it.
template<class _Tp>
inline const bool __desugars_to_v<__equal_tag, equal_to<_Tp>, _Tp, _Tp> = true;

// In the transparent case, we do not enforce that
template<class _Tp, class _Up>
inline const bool __desugars_to_v<__equal_tag, equal_to<void>, _Tp, _Up> = true;

template<class _Tp = void>
struct not_equal_to : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x != __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(not_equal_to);

template<>
struct not_equal_to<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) != kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) != kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) != kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp>
struct less : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x < __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(less);

template<class _Tp>
inline const bool __desugars_to_v<__less_tag, less<_Tp>, _Tp, _Tp> = true;

template<class _Tp>
inline const bool __desugars_to_v<__totally_ordered_less_tag, less<_Tp>, _Tp, _Tp> = is_integral<_Tp>::value;

template<>
struct less<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) < kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) < kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) < kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp>
struct __make_transparent<_Tp, less<_Tp>> {
  using type = less<>;
};

template<>
inline const bool __is_generic_transparent_comparator_v<less<>> = true;

template<class _Tp, class _Up>
inline const bool __desugars_to_v<__less_tag, less<>, _Tp, _Up> = true;

template<class _Tp>
inline const bool __desugars_to_v<__totally_ordered_less_tag, less<>, _Tp, _Tp> = is_integral<_Tp>::value;

template<class _Tp = void>
struct less_equal : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x <= __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(less_equal);

template<>
struct less_equal<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) <= kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) <= kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) <= kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct greater_equal : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x >= __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(greater_equal);

template<>
struct greater_equal<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) >=
                        kstd::forward<_T2>(__u))) -> decltype(kstd::forward<_T1>(__t) >= kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) >= kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct greater : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x > __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(greater);

template<class _Tp>
inline const bool __desugars_to_v<__greater_tag, greater<_Tp>, _Tp, _Tp> = true;

template<>
struct greater<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) > kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) > kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) > kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp, class _Up>
inline const bool __desugars_to_v<__greater_tag, greater<>, _Tp, _Up> = true;

template<class _Tp>
struct __make_transparent<_Tp, greater<_Tp>> {
  using type = greater<>;
};

template<>
inline const bool __is_generic_transparent_comparator_v<greater<>> = true;

// Logical operations

template<class _Tp = void>
struct logical_and : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x && __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(logical_and);

template<>
struct logical_and<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) && kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) && kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) && kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct logical_not : __unary_function<_Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x) const { return !__x; }
};
_CTAD_SUPPORTED_FOR_TYPE(logical_not);

template<>
struct logical_not<void> {
  template<class _Tp>
  constexpr auto operator()(_Tp &&__x) const
      noexcept(noexcept(!kstd::forward<_Tp>(__x)))//
      -> decltype(!kstd::forward<_Tp>(__x)) {
    return !kstd::forward<_Tp>(__x);
  }
  typedef void is_transparent;
};

template<class _Tp = void>
struct logical_or : __binary_function<_Tp, _Tp, bool> {
  typedef bool __result_type;// used by valarray
  constexpr bool operator()(const _Tp &__x, const _Tp &__y) const {
    return __x || __y;
  }
};
_CTAD_SUPPORTED_FOR_TYPE(logical_or);

template<>
struct logical_or<void> {
  template<class _T1, class _T2>
  constexpr auto operator()(_T1 &&__t, _T2 &&__u) const
      noexcept(noexcept(kstd::forward<_T1>(__t) || kstd::forward<_T2>(__u)))//
      -> decltype(kstd::forward<_T1>(__t) || kstd::forward<_T2>(__u)) {
    return kstd::forward<_T1>(__t) || kstd::forward<_T2>(__u);
  }
  typedef void is_transparent;
};


}// namespace kstd
