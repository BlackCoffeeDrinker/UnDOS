
#pragma once

#include <__concepts/copyable.hpp>
#include <__config.hpp>
#include <__iterator/concepts.hpp>

namespace kstd {
namespace ranges {
enum class subrange_kind : bool { unsized,
                                  sized };

template<input_or_output_iterator _Iter, sentinel_for<_Iter> _Sent, subrange_kind _Kind>
  requires(_Kind == subrange_kind::sized || !sized_sentinel_for<_Sent, _Iter>)
class subrange;

template<size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires((_Index == 0 && copyable<_Iter>) || _Index == 1)
constexpr auto get(const subrange<_Iter, _Sent, _Kind> &);

template<size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires(_Index < 2)
constexpr auto get(subrange<_Iter, _Sent, _Kind> &&);

}// namespace ranges

using ranges::get;

}// namespace kstd
