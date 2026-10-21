
#pragma once

#include <__config.hpp>
#include <__type_traits/is_class.hpp>
#include <__type_traits/is_enum.hpp>
#include <__type_traits/is_union.hpp>

namespace kstd {

template<class _Tp>
concept __class_or_enum = is_class_v<_Tp> || is_union_v<_Tp> || is_enum_v<_Tp>;

}
