
#pragma once

#include <__config.hpp>

#include <__concepts/convertible_to.hpp>
#include <__concepts/same_as.hpp>
#include <__type_traits/common_reference.hpp>

namespace kstd {

template<class _Tp, class _Up>
concept common_reference_with =
    same_as<common_reference_t<_Tp, _Up>, common_reference_t<_Up, _Tp>> &&
    convertible_to<_Tp, common_reference_t<_Tp, _Up>> && convertible_to<_Up, common_reference_t<_Tp, _Up>>;

}
