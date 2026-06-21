#pragma once

#include <__concepts/assignable.hpp>
#include <__concepts/constructible.hpp>
#include <__concepts/swappable.hpp>
#include <__config.hpp>
#include <__type_traits/is_object.hpp>

namespace kstd {

template<class _Tp>
concept movable = is_object_v<_Tp> && move_constructible<_Tp> && assignable_from<_Tp &, _Tp> && swappable<_Tp>;

}// namespace kstd
