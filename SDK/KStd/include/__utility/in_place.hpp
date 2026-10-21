
#pragma once
#include <__config.hpp>

namespace kstd {

struct in_place_t {
    explicit in_place_t() = default;
};

inline constexpr in_place_t in_place{};

template <class _Tp>
struct in_place_type_t {
    explicit in_place_type_t() = default;
};

template <class _Tp>
inline constexpr in_place_type_t<_Tp> in_place_type{};

template <size_t _Idx>
struct in_place_index_t {
    explicit in_place_index_t() = default;
};

template <size_t _Idx>
inline constexpr in_place_index_t<_Idx> in_place_index{};

} // namespace kstd
