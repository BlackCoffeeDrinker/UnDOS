
#pragma once

#include <__config.hpp>

namespace kstd {
template<bool>
struct _IfImpl;

template<>
struct _IfImpl<true> {
  template<class _IfRes, class _ElseRes>
  using _Select = _IfRes;
};

template<>
struct _IfImpl<false> {
  template<class _IfRes, class _ElseRes>
  using _Select = _ElseRes;
};

template<bool _Cond, class _IfRes, class _ElseRes>
using _If = typename _IfImpl<_Cond>::template _Select<_IfRes, _ElseRes>;

template<bool _Bp, class _If, class _Then>
struct conditional {
  using type = _If;
};

template<class _If, class _Then>
struct conditional<false, _If, _Then> {
  using type = _Then;
};

template<bool _Bp, class _IfRes, class _ElseRes>
using conditional_t = typename conditional<_Bp, _IfRes, _ElseRes>::type;

// Helper so we can use "conditional_t" in all language versions.
template<bool _Bp, class _If, class _Then>
using __conditional_t = typename conditional<_Bp, _If, _Then>::type;

}// namespace kstd
