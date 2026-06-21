
#pragma once
#include <__config.hpp>

#include <limits.h>
#include <stddef.hpp>

namespace kstd {

namespace detail {
template<typename T>
constexpr auto signed_b() {
  return static_cast<T>(-1) < 0;
}

template<typename T>
constexpr auto digits_b(size_t B) {
  return B - signed_b<T>();
}
template<typename T>
constexpr auto digits10_b(size_t B) {
  return (digits_b<T>(B) * 643L / 2136);
}

template<typename T>
constexpr auto digits() {
  return digits_b<T>(sizeof(T) * __CHAR_BIT__);
}
template<typename T>
constexpr auto digits10() {
  return digits10_b<T>(sizeof(T) * __CHAR_BIT__);
}
}// namespace detail

#define __digits(T) (detail::digits<T>())
#define __digits10(T) (detail::digits10<T>())

enum float_round_style {
  round_indeterminate = -1,    ///< Intermediate.
  round_toward_zero = 0,       ///< To zero.
  round_to_nearest = 1,        ///< To the nearest representable value.
  round_toward_infinity = 2,   ///< To infinity.
  round_toward_neg_infinity = 3///< To negative infinity.
};

enum float_denorm_style {
  /// Indeterminate at compile time whether denormalized values are allowed.
  denorm_indeterminate = -1,
  /// The type does not allow denormalized values.
  denorm_absent = 0,
  /// The type allows denormalized values.
  denorm_present = 1
};

template<typename>
struct numeric_limits {
  static constexpr bool is_specialized = false;
};

template<typename _Tp>
struct numeric_limits<const _Tp> : public numeric_limits<_Tp> {};

template<typename _Tp>
struct numeric_limits<volatile _Tp> : public numeric_limits<_Tp> {};

template<typename _Tp>
struct numeric_limits<const volatile _Tp> : public numeric_limits<_Tp> {};


template<>
struct numeric_limits<bool> {
  static constexpr bool is_specialized = true;
  static constexpr bool min() noexcept { return false; }
  static constexpr bool max() noexcept { return true; }
  static constexpr bool lowest() noexcept { return min(); }
  static constexpr int digits = 1;
  static constexpr int digits10 = 0;
  static constexpr int max_digits10 = 0;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr int radix = 2;
  static constexpr bool epsilon() noexcept { return false; }
  static constexpr bool round_error() noexcept { return false; }
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool infinity() noexcept { return false; }
  static constexpr bool quiet_NaN() noexcept { return false; }
  static constexpr bool signaling_NaN() noexcept { return false; }
  static constexpr bool denorm_min() noexcept { return false; }
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_toward_zero;
};

template<>
struct numeric_limits<char> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 7;
  static constexpr int digits10 = 2;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr char min() noexcept { return -128; }
  static constexpr char lowest() noexcept { return -128; }
  static constexpr char max() noexcept { return 127; }
  static constexpr char epsilon() noexcept { return 0; }
  static constexpr char round_error() noexcept { return 0; }
  static constexpr char infinity() noexcept { return 0; }
  static constexpr char quiet_NaN() noexcept { return 0; }
  static constexpr char signaling_NaN() noexcept { return 0; }
  static constexpr char denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<signed char> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 7;
  static constexpr int digits10 = 2;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr signed char min() noexcept { return -128; }
  static constexpr signed char lowest() noexcept { return -128; }
  static constexpr signed char max() noexcept { return 127; }
  static constexpr signed char epsilon() noexcept { return 0; }
  static constexpr signed char round_error() noexcept { return 0; }
  static constexpr signed char infinity() noexcept { return 0; }
  static constexpr signed char quiet_NaN() noexcept { return 0; }
  static constexpr signed char signaling_NaN() noexcept { return 0; }
  static constexpr signed char denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<unsigned char> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = 8;
  static constexpr int digits10 = 2;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr unsigned char min() noexcept { return 0; }
  static constexpr unsigned char lowest() noexcept { return 0; }
  static constexpr unsigned char max() noexcept { return 255; }
  static constexpr unsigned char epsilon() noexcept { return 0; }
  static constexpr unsigned char round_error() noexcept { return 0; }
  static constexpr unsigned char infinity() noexcept { return 0; }
  static constexpr unsigned char quiet_NaN() noexcept { return 0; }
  static constexpr unsigned char signaling_NaN() noexcept { return 0; }
  static constexpr unsigned char denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<short> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 15;
  static constexpr int digits10 = 4;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr short min() noexcept { return -32768; }
  static constexpr short lowest() noexcept { return -32768; }
  static constexpr short max() noexcept { return 32767; }
  static constexpr short epsilon() noexcept { return 0; }
  static constexpr short round_error() noexcept { return 0; }
  static constexpr short infinity() noexcept { return 0; }
  static constexpr short quiet_NaN() noexcept { return 0; }
  static constexpr short signaling_NaN() noexcept { return 0; }
  static constexpr short denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<unsigned short> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = 16;
  static constexpr int digits10 = 4;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr unsigned short min() noexcept { return 0; }
  static constexpr unsigned short lowest() noexcept { return 0; }
  static constexpr unsigned short max() noexcept { return 65535; }
  static constexpr unsigned short epsilon() noexcept { return 0; }
  static constexpr unsigned short round_error() noexcept { return 0; }
  static constexpr unsigned short infinity() noexcept { return 0; }
  static constexpr unsigned short quiet_NaN() noexcept { return 0; }
  static constexpr unsigned short signaling_NaN() noexcept { return 0; }
  static constexpr unsigned short denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<int> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 31;
  static constexpr int digits10 = 9;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr int min() noexcept { return -2147483648; }
  static constexpr int lowest() noexcept { return -2147483648; }
  static constexpr int max() noexcept { return 2147483647; }
  static constexpr int epsilon() noexcept { return 0; }
  static constexpr int round_error() noexcept { return 0; }
  static constexpr int infinity() noexcept { return 0; }
  static constexpr int quiet_NaN() noexcept { return 0; }
  static constexpr int signaling_NaN() noexcept { return 0; }
  static constexpr int denorm_min() noexcept { return 0; }
};

template<>
struct numeric_limits<unsigned int> {
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr bool has_denorm = false;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = 32;
  static constexpr int digits10 = 9;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;

  static constexpr unsigned int min() noexcept { return 0; }
  static constexpr unsigned int lowest() noexcept { return 0; }
  static constexpr unsigned int max() noexcept { return 4294967295; }
  static constexpr unsigned int epsilon() noexcept { return 0; }
  static constexpr unsigned int round_error() noexcept { return 0; }
  static constexpr unsigned int infinity() noexcept { return 0; }
  static constexpr unsigned int quiet_NaN() noexcept { return 0; }
  static constexpr unsigned int signaling_NaN() noexcept { return 0; }
  static constexpr unsigned int denorm_min() noexcept { return 0; }
};


/// numeric_limits<long> specialization.
template<>
struct numeric_limits<long> {
  static constexpr bool is_specialized = true;

  static constexpr int digits = __digits(long);
  static constexpr int digits10 = __digits10(long);
  static constexpr int max_digits10 = 0;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr int radix = 2;

  static constexpr long epsilon() noexcept { return 0; }
  static constexpr long round_error() noexcept { return 0; }
  static constexpr long min() noexcept { return -__LONG_MAX__ - 1; }
  static constexpr long max() noexcept { return __LONG_MAX__; }
  static constexpr long lowest() noexcept { return min(); }
  static constexpr long infinity() noexcept { return static_cast<long>(0); }
  static constexpr long quiet_NaN() noexcept { return static_cast<long>(0); }
  static constexpr long signaling_NaN() noexcept { return static_cast<long>(0); }
  static constexpr long denorm_min() noexcept { return static_cast<long>(0); }

  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_toward_zero;
};

/// numeric_limits<unsigned long> specialization.
template<>
struct numeric_limits<unsigned long> {
  static constexpr bool is_specialized = true;
  static constexpr unsigned long min() noexcept { return 0; }
  static constexpr unsigned long max() noexcept { return __LONG_MAX__ * 2UL + 1; }
  static constexpr unsigned long lowest() noexcept { return min(); }
  static constexpr unsigned long epsilon() noexcept { return 0; }
  static constexpr unsigned long round_error() noexcept { return 0; }
  static constexpr unsigned long infinity() noexcept { return static_cast<unsigned long>(0); }
  static constexpr unsigned long quiet_NaN() noexcept { return static_cast<unsigned long>(0); }
  static constexpr unsigned long signaling_NaN() noexcept { return static_cast<unsigned long>(0); }
  static constexpr unsigned long denorm_min() noexcept { return static_cast<unsigned long>(0); }

  static constexpr int digits = __digits(unsigned long);
  static constexpr int digits10 = __digits10(unsigned long);
  static constexpr int max_digits10 = 0;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_toward_zero;
};

/// numeric_limits<long long> specialization.
template<>
struct numeric_limits<long long> {
  static constexpr bool is_specialized = true;

  static constexpr long long min() noexcept { return -__LONG_LONG_MAX__ - 1; }
  static constexpr long long max() noexcept { return __LONG_LONG_MAX__; }
  static constexpr long long lowest() noexcept { return min(); }
  static constexpr long long epsilon() noexcept { return 0; }
  static constexpr long long round_error() noexcept { return 0; }
  static constexpr long long infinity() noexcept { return static_cast<long long>(0); }
  static constexpr long long quiet_NaN() noexcept { return static_cast<long long>(0); }
  static constexpr long long signaling_NaN() noexcept { return static_cast<long long>(0); }
  static constexpr long long denorm_min() noexcept { return static_cast<long long>(0); }

  static constexpr int digits = __digits(long long);
  static constexpr int digits10 = __digits10(long long);
  static constexpr int max_digits10 = 0;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_toward_zero;
};

/// numeric_limits<unsigned long long> specialization.
template<>
struct numeric_limits<unsigned long long> {
  static constexpr bool is_specialized = true;

  static constexpr unsigned long long min() noexcept { return 0; }
  static constexpr unsigned long long max() noexcept { return __LONG_LONG_MAX__ * 2ULL + 1; }
  static constexpr unsigned long long lowest() noexcept { return min(); }
  static constexpr unsigned long long epsilon() noexcept { return 0; }
  static constexpr unsigned long long round_error() noexcept { return 0; }
  static constexpr unsigned long long infinity() noexcept { return static_cast<unsigned long long>(0); }
  static constexpr unsigned long long quiet_NaN() noexcept { return static_cast<unsigned long long>(0); }
  static constexpr unsigned long long signaling_NaN() noexcept { return static_cast<unsigned long long>(0); }
  static constexpr unsigned long long denorm_min() noexcept { return static_cast<unsigned long long>(0); }

  static constexpr int digits = __digits(unsigned long long);
  static constexpr int digits10 = __digits10(unsigned long long);
  static constexpr int max_digits10 = 0;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr bool traps = true;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_toward_zero;
};


}// namespace kstd
