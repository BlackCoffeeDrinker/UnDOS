
#pragma once

#include <__algo/comp.hpp>
#include <__algo/comp_ref_type.hpp>
#include <__algo/max_element.hpp>
#include <__config.hpp>
#include <initializer_list.hpp>

namespace kstd {

template<class _Tp, class _Compare>
[[__nodiscard__]] inline _KSTD_API constexpr const _Tp &
max(const _Tp &__a, const _Tp &__b, _Compare __comp) {
  return __comp(__a, __b) ? __b : __a;
}

template<class _Tp>
[[__nodiscard__]] inline _KSTD_API constexpr const _Tp &
max(const _Tp &__a, const _Tp &__b) {
  return kstd::max(__a, __b, __less<>());
}

template<class _Tp, class _Compare>
[[__nodiscard__]] inline _KSTD_API constexpr _Tp
max(initializer_list<_Tp> __t, _Compare __comp) {
  return *kstd::__max_element<__comp_ref_type<_Compare>>(__t.begin(), __t.end(), __comp);
}

template<class _Tp>
[[__nodiscard__]] inline _KSTD_API constexpr _Tp max(initializer_list<_Tp> __t) {
  return *kstd::max_element(__t.begin(), __t.end(), __less<>());
}


}// namespace kstd
