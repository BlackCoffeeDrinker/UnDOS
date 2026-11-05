
#pragma once

#include <__concepts/assignable.hpp>
#include <__concepts/constructible.hpp>
#include <__concepts/movable.hpp>
#include <__config.hpp>

namespace kstd {

// [concepts.object]

// clang-format off
template <class _Tp>
concept copyable =
    copy_constructible<_Tp> &&
    movable<_Tp> &&
    assignable_from<_Tp&, _Tp&> &&
    assignable_from<_Tp&, const _Tp&> &&
    assignable_from<_Tp&, const _Tp>;
// clang-format on


}// namespace kstd
