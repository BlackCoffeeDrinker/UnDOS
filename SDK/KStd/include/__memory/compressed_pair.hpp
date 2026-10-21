
#pragma once

#include <__config.hpp>
#include <__type_traits/conditional.hpp>
#include <__type_traits/is_empty.hpp>
#include <__type_traits/is_final.hpp>
#include <__utility/forward.hpp>
#include <__utility/move.hpp>

namespace kstd {

template <class _Tp, int _Idx, bool _CanEBCO = is_empty<_Tp>::value && !is_final<_Tp>::value>
struct __compressed_pair_elem {
  _Tp __value_;
  constexpr __compressed_pair_elem() : __value_() {}
  template <class _Up>
  constexpr explicit __compressed_pair_elem(_Up&& __u) : __value_(kstd::forward<_Up>(__u)) {}

  constexpr _Tp& __get() noexcept { return __value_; }
  constexpr const _Tp& __get() const noexcept { return __value_; }
};

template <class _Tp, int _Idx>
struct __compressed_pair_elem<_Tp, _Idx, true> : private _Tp {
  constexpr __compressed_pair_elem() : _Tp() {}
  template <class _Up>
  constexpr explicit __compressed_pair_elem(_Up&& __u) : _Tp(kstd::forward<_Up>(__u)) {}

  constexpr _Tp& __get() noexcept { return *this; }
  constexpr const _Tp& __get() const noexcept { return *this; }
};

template <class _T1, class _T2>
class __compressed_pair : private __compressed_pair_elem<_T1, 0>,
                          private __compressed_pair_elem<_T2, 1> {
  using _Base1 = __compressed_pair_elem<_T1, 0>;
  using _Base2 = __compressed_pair_elem<_T2, 1>;

public:
  constexpr __compressed_pair() = default;

  template <class _U1, class _U2>
  constexpr explicit __compressed_pair(_U1&& __u1, _U2&& __u2)
      : _Base1(kstd::forward<_U1>(__u1)), _Base2(kstd::forward<_U2>(__u2)) {}

  constexpr _T1& first() noexcept { return static_cast<_Base1&>(*this).__get(); }
  constexpr const _T1& first() const noexcept { return static_cast<const _Base1&>(*this).__get(); }

  constexpr _T2& second() noexcept { return static_cast<_Base2&>(*this).__get(); }
  constexpr const _T2& second() const noexcept { return static_cast<const _Base2&>(*this).__get(); }
};

} // namespace kstd
