
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Tp, size_t _Size>
struct array;

template<size_t _Ip, class _Tp, size_t _Size>
constexpr _Tp &get(array<_Tp, _Size> &) noexcept;

template<size_t _Ip, class _Tp, size_t _Size>
constexpr const _Tp &get(const array<_Tp, _Size> &) noexcept;

#ifndef _LIBCPP_CXX03_LANG
template<size_t _Ip, class _Tp, size_t _Size>
constexpr _Tp &&get(array<_Tp, _Size> &&) noexcept;

template<size_t _Ip, class _Tp, size_t _Size>
constexpr const _Tp &&get(const array<_Tp, _Size> &&) noexcept;
#endif

template<class _Tp>
inline const bool __is_std_array_v = false;

template<class _Tp, size_t _Size>
inline const bool __is_std_array_v<array<_Tp, _Size>> = true;

}// namespace kstd
