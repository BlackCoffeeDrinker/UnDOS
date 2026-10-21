#pragma once

#include <__concepts/convertible_to.hpp>
#include <__concepts/same_as.hpp>
#include <__config.hpp>
#include <__type_traits/common_reference.hpp>

namespace kstd {
template<class _Tp, class _Up, class _CommonRef = common_reference_t<const _Tp &, const _Up &>>
concept __comparison_common_type_with_impl =
    same_as<common_reference_t<const _Tp &, const _Up &>, common_reference_t<const _Up &, const _Tp &>> && requires {
      requires convertible_to<const _Tp &, const _CommonRef &> || convertible_to<_Tp, const _CommonRef &>;
      requires convertible_to<const _Up &, const _CommonRef &> || convertible_to<_Up, const _CommonRef &>;
    };

template<class _Tp, class _Up>
concept __comparison_common_type_with = __comparison_common_type_with_impl<remove_cvref_t<_Tp>, remove_cvref_t<_Up>>;

}// namespace kstd
