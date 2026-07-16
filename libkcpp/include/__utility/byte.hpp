
#pragma once

#include <__config.hpp>

namespace kstd {

enum class byte : unsigned char {};

_KSTD_API inline constexpr byte operator|(byte __lhs, byte __rhs) noexcept {
  return static_cast<byte>(
      static_cast<unsigned char>(static_cast<unsigned int>(__lhs) | static_cast<unsigned int>(__rhs)));
}

_KSTD_API inline constexpr byte& operator|=(byte& __lhs, byte __rhs) noexcept {
  return __lhs = __lhs | __rhs;
}

_KSTD_API inline constexpr byte operator&(byte __lhs, byte __rhs) noexcept {
  return static_cast<byte>(
      static_cast<unsigned char>(static_cast<unsigned int>(__lhs) & static_cast<unsigned int>(__rhs)));
}

_KSTD_API inline constexpr byte& operator&=(byte& __lhs, byte __rhs) noexcept {
  return __lhs = __lhs & __rhs;
}

_KSTD_API inline constexpr byte operator^(byte __lhs, byte __rhs) noexcept {
  return static_cast<byte>(
      static_cast<unsigned char>(static_cast<unsigned int>(__lhs) ^ static_cast<unsigned int>(__rhs)));
}

_KSTD_API inline constexpr byte& operator^=(byte& __lhs, byte __rhs) noexcept {
  return __lhs = __lhs ^ __rhs;
}

_KSTD_API inline constexpr byte operator~(byte __b) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(~static_cast<unsigned int>(__b)));
}

template <class _Integer, enable_if_t<is_integral<_Integer>::value, int> = 0>
_KSTD_API constexpr byte& operator<<=(byte& __lhs, _Integer __shift) noexcept {
  return __lhs = __lhs << __shift;
}

template <class _Integer, enable_if_t<is_integral<_Integer>::value, int> = 0>
_KSTD_API constexpr byte operator<<(byte __lhs, _Integer __shift) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(__lhs) << __shift));
}

template <class _Integer, enable_if_t<is_integral<_Integer>::value, int> = 0>
_KSTD_API constexpr byte& operator>>=(byte& __lhs, _Integer __shift) noexcept {
  return __lhs = __lhs >> __shift;
}

template <class _Integer, enable_if_t<is_integral<_Integer>::value, int> = 0>
_KSTD_API constexpr byte operator>>(byte __lhs, _Integer __shift) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(__lhs) >> __shift));
}

template <class _Integer, enable_if_t<is_integral<_Integer>::value, int> = 0>
[[nodiscard]] _KSTD_API constexpr _Integer to_integer(byte __b) noexcept {
  return static_cast<_Integer>(__b);
}
}
