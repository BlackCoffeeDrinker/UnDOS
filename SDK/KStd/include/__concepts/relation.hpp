
#pragma once

#include <__concepts/predicate.hpp>
#include <__config.hpp>

namespace kstd {

template<class _Rp, class _Tp, class _Up>
concept relation =
    predicate<_Rp, _Tp, _Tp> && predicate<_Rp, _Up, _Up> && predicate<_Rp, _Tp, _Up> && predicate<_Rp, _Up, _Tp>;

// [concept.equiv]

template<class _Rp, class _Tp, class _Up>
concept equivalence_relation = relation<_Rp, _Tp, _Up>;

// [concept.strictweakorder]

template<class _Rp, class _Tp, class _Up>
concept strict_weak_order = relation<_Rp, _Tp, _Up>;

}// namespace kstd
