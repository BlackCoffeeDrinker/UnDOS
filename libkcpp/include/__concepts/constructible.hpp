#pragma once

#include <__config.hpp>

#include <__concepts/convertible_to.hpp>
#include <__concepts/destructible.hpp>
#include <__type_traits/is_constructible.hpp>

namespace kstd {
// [concept.constructible]
template<class _Tp, class... _Args>
concept constructible_from = destructible<_Tp> && is_constructible_v<_Tp, _Args...>;

// [concept.default.init]

template<class _Tp>
concept __default_initializable = requires { ::new _Tp; };

template<class _Tp>
concept default_initializable = constructible_from<_Tp> && requires { _Tp{}; } && __default_initializable<_Tp>;

// [concept.moveconstructible]
template<class _Tp>
concept move_constructible = constructible_from<_Tp, _Tp> && convertible_to<_Tp, _Tp>;

// [concept.copyconstructible]
// clang-format off
template <class _Tp>
concept copy_constructible =
    move_constructible<_Tp> &&
    constructible_from<_Tp, _Tp&> && convertible_to<_Tp&, _Tp> &&
    constructible_from<_Tp, const _Tp&> && convertible_to<const _Tp&, _Tp> &&
    constructible_from<_Tp, const _Tp> && convertible_to<const _Tp, _Tp>;
// clang-format on

}// namespace kstd
