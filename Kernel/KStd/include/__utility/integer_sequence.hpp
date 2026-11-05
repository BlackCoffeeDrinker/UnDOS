
#pragma once
#include <__config.hpp>
#include <__tuple/tuple_element.hpp>
#include <__tuple/tuple_size.hpp>
#include <__type_traits/is_integral.hpp>

namespace kstd {

#if __has_builtin(__make_integer_seq)
template<template<class _Tp, _Tp...> class _BaseType, class _Tp, _Tp _SequenceSize>
using __make_integer_sequence_impl = __make_integer_seq<_BaseType, _Tp, _SequenceSize>;
#else
template<template<class _Tp, _Tp...> class _BaseType, class _Tp, _Tp _SequenceSize>
using __make_integer_sequence_impl = _BaseType<_Tp, __integer_pack(_SequenceSize)...>;
#endif

template<class _Tp, _Tp... _Indices>
struct __integer_sequence {
  using value_type = _Tp;
  static_assert(is_integral<_Tp>::value, "std::integer_sequence can only be instantiated with an integral type");
  [[__nodiscard__]] static constexpr size_t size() noexcept { return sizeof...(_Indices); }
};

template<size_t... _Indices>
using __index_sequence = __integer_sequence<size_t, _Indices...>;

template<size_t _SequenceSize>
using __make_index_sequence = __make_integer_sequence_impl<__integer_sequence, size_t, _SequenceSize>;

template<class... _Args>
using __index_sequence_for = __make_index_sequence<sizeof...(_Args)>;


template<class _Tp, _Tp... _Indices>
struct integer_sequence : __integer_sequence<_Tp, _Indices...> {};

template<size_t... _Ip>
using index_sequence = integer_sequence<size_t, _Ip...>;

template<class _Tp, _Tp _Ep>
using make_integer_sequence = __make_integer_sequence_impl<integer_sequence, _Tp, _Ep>;

template<size_t _Np>
using make_index_sequence = make_integer_sequence<size_t, _Np>;

template<class... _Tp>
using index_sequence_for = make_index_sequence<sizeof...(_Tp)>;

// Executes __func for every element in an index_sequence.
template<size_t... _Index, class _Function>
constexpr void __for_each_index_sequence(index_sequence<_Index...>, _Function __func) {
  (__func.template operator()<_Index>(), ...);
}


}// namespace kstd
