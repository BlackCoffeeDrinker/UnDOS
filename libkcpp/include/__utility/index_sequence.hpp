
#pragma once
#include <__config.hpp>
#include <__utility/integer_sequence.hpp>
#include <stddef.hpp>

namespace kstd {
template<size_t... _Idx>
using index_sequence = integer_sequence<size_t, _Idx...>;

/// Alias template make_index_sequence
template<size_t _Num>
using make_index_sequence = make_integer_sequence<size_t, _Num>;

}// namespace kstd
