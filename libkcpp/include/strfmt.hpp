
#pragma once
#include <__config.hpp>

#include "__utility/forward.hpp"
#include "array.hpp"
#include "nomm_string.hpp"
#include "string_view.hpp"
#include "type_traits.hpp"
#include "static_string.hpp"
#include "span.hpp"

namespace kstd {
namespace detail {
struct format_options {
  enum class NumberFormat {
    decimal,
    hexadecimal,
  };
  enum class PadType {
    none,
    left,
    right
  };

  NumberFormat integer_format = NumberFormat::decimal;
  PadType pad_type = PadType::none;
  char pad_value = 0;
  size_t pad_length = 0;
};

struct format_arg {
  enum class arg_type {
    none_type,
    int_type,
    uint_type,
    ulong_type,
    slong_type,
    bool_type,
    char_type,
    float_type,
    double_type,
    long_double_type,
    string_view_type,
    pointer_type,
    long_long_type,
    ulong_long_type,
  };

  arg_type type;

  union {
    int int_value;
    unsigned int uint_value;
    unsigned long ulong_value;
    long slong_value;
    bool bool_value;
    char char_value;
    float float_value;
    double double_value;
    // long double long_double_value;
    const void *pointer;
    string_view str_view;
    long long long_long_value;
    unsigned long long ulong_long_value;
  };

  constexpr format_arg() : type(arg_type::none_type), pointer(nullptr) {}
  explicit constexpr format_arg(const char *str) : type(arg_type::string_view_type), str_view(str) {}
  explicit constexpr format_arg(string_view sv) : type(arg_type::string_view_type), str_view(sv) {}
  template<size_t N>
  explicit constexpr format_arg(const static_string<N>& static_string) : type(arg_type::string_view_type), str_view(static_string.data(), static_string.length()) {}
  template<typename Traits>
  explicit constexpr format_arg(const basic_nomm_string<char, Traits>& nomm_str) : type(arg_type::string_view_type), str_view(nomm_str.data(), nomm_str.length()) {}
  explicit constexpr format_arg(const void *ptr) : type(arg_type::pointer_type), pointer(ptr) {}
  //explicit constexpr format_arg(long double ldv) : type(arg_type::long_double_type), long_double_value(ldv) {}
  explicit constexpr format_arg(double dv) : type(arg_type::double_type), double_value(dv) {}
  explicit constexpr format_arg(float fv) : type(arg_type::float_type), float_value(fv) {}
  explicit constexpr format_arg(char cv) : type(arg_type::char_type), char_value(cv) {}
  explicit constexpr format_arg(bool bv) : type(arg_type::bool_type), bool_value(bv) {}
  explicit constexpr format_arg(unsigned uiv) : type(arg_type::uint_type), uint_value(uiv) {}
  explicit constexpr format_arg(int iv) : type(arg_type::int_type), int_value(iv) {}
  explicit constexpr format_arg(unsigned long ul) : type(arg_type::ulong_type), ulong_value(ul) {}
  // A plain `long` needs its own overload -- without it, a `long` argument
  // (e.g. int32_t on toolchains where stdint.h defines int32_t as `long`,
  // as i686-elf's does) is ambiguous between the `int` and `long long`
  // overloads above, since both are equally-ranked standard conversions.
  explicit constexpr format_arg(long lv) : type(arg_type::slong_type), slong_value(lv) {}
  explicit constexpr format_arg(unsigned long long ullv) : type(arg_type::ulong_long_type), ulong_long_value(ullv) {}
  explicit constexpr format_arg(long long llv) : type(arg_type::long_long_type), long_long_value(llv) {}

  [[nodiscard]] constexpr bool is_numeric() const {
    return // type == arg_type::long_double_type ||
           type == arg_type::double_type ||
           type == arg_type::float_type ||
           type == arg_type::ulong_long_type ||
           type == arg_type::long_long_type ||
           type == arg_type::uint_type ||
           type == arg_type::int_type ||
           type == arg_type::slong_type ||
           type == arg_type::ulong_type;
  }
};

template<typename T>
format_arg make_arg(const T& arg) {
  if constexpr (kstd::is_enum_v<T>) {
    return format_arg(kstd::underlying_type_t<T>(arg));
  } else {
    return format_arg(arg);
  }
}

inline format_options make_options(const string_view &options_string) {
  format_options options;
  if (options_string.empty()) return options;

  for (size_t i = 0; i < options_string.length(); ++i) {
    if (options_string[i] == 'x') {
      options.integer_format = format_options::NumberFormat::hexadecimal;
    }
    // Add more parsing logic if needed
  }

  return options;
}

template<typename... Args>
array<format_arg, sizeof...(Args)> make_args(const Args&... args) {
  return {make_arg(args)...};
}

template<typename T>
constexpr unsigned int num_digits(T number) {
  unsigned int digits = 0;
  if (number < 0)
    digits = 1;
  while (number) {
    number /= 10;
    digits++;
  }
  return digits;
}

inline size_t format_unsigned(char (&buffer)[32], unsigned long long value, unsigned int base) {
  size_t size = 0;
  do {
    const auto remainder = static_cast<unsigned int>(value % base);
    buffer[size++] = static_cast<char>((remainder < 10) ? (remainder + '0') : (remainder - 10 + 'a'));
    value /= base;
  } while (value != 0);
  return size;
}

template<typename DESTINATION>
inline void handlePrintArgument(DESTINATION &target, const format_arg &arg, const format_options &options) {
  unsigned long long value = 0;
  unsigned int base = options.integer_format == format_options::NumberFormat::hexadecimal ? 16 : 10;
  switch (arg.type) {
    case format_arg::arg_type::uint_type:
      value = arg.uint_value;
      break;
    case format_arg::arg_type::int_type:
      value = static_cast<unsigned long long>(arg.int_value);
      if (arg.int_value < 0) {
        target('-');
        value = 0ull - value;
        base = 10;
      }
      break;
    case format_arg::arg_type::ulong_type:
      value = arg.ulong_value;
      break;
    case format_arg::arg_type::slong_type:
      value = static_cast<unsigned long long>(arg.slong_value);
      if (arg.slong_value < 0) {
        target('-');
        value = 0ull - value;
        base = 10;
      }
      break;
    case format_arg::arg_type::ulong_long_type:
      value = arg.ulong_long_value;
      break;
    case format_arg::arg_type::string_view_type:
      for (size_t i = 0; i < arg.str_view.length(); ++i) {
        target(arg.str_view[i]);
      }
      return;
    case format_arg::arg_type::char_type:
      target(arg.char_value);
      return;
    case format_arg::arg_type::pointer_type:
      target('0');
      target('x');
      value = reinterpret_cast<uintptr_t>(arg.pointer);
      base = 16;
      break;
    case format_arg::arg_type::bool_type:
      if (arg.bool_value) {
        target('T');
        target('r');
        target('u');
        target('e');
      } else {
        target('F');
        target('a');
        target('l');
        target('s');
        target('e');
      }
      return;
    default:
      // Fallback for unhandled types
      return;
  }

  char buffer[32];
  size_t size = format_unsigned(buffer, value, base);
  while (size > 0) target(buffer[--size]);
}

template<typename DESTINATION>
void format_args_to(DESTINATION &destination, const string_view &fmt, span<const format_arg> fmt_args) {
  size_t current_pos = 0;
  size_t next_unnumbered_arg = 0;

  while (current_pos != fmt.length()) {
    if (const auto current = fmt[current_pos];
        current == '{') {
      // Find the closing '}'
      const auto closing = fmt.find('}', current_pos + 1);
      if (closing == string_view::npos) {
        return;
      }

      // Do we have something between {} ?
      if (closing - current_pos > 1) {
        const auto options = detail::make_options(fmt.substr(current_pos + 1, closing - current_pos - 1));
        if (next_unnumbered_arg < fmt_args.size()) {
          detail::handlePrintArgument(destination, fmt_args[next_unnumbered_arg], options);
          next_unnumbered_arg++;
        }
      } else {
        if (next_unnumbered_arg < fmt_args.size()) {
          detail::handlePrintArgument(destination, fmt_args[next_unnumbered_arg], {});
          next_unnumbered_arg++;
        }
      }

      current_pos = closing;
    } else {
      destination(current);
    }

    current_pos++;
  }
}

template<typename STRING>
struct append_format_sink {
  STRING &destination;

  void operator()(char c) const {
    destination.append(c);
  }
};

}// namespace detail

template<typename DESTINATION, typename... Args>
void format_dst(DESTINATION destination, const string_view &fmt, Args &&...args) {
  const auto fmt_args = detail::make_args(args...);
  detail::format_args_to(destination, fmt, span<const detail::format_arg>(fmt_args));
}

template<typename... Args>
void format(nomm_string &destination, string_view fmt, Args &&...args) {
  destination.clear();
  detail::append_format_sink<nomm_string> target{destination};
  format_dst(target, fmt, kstd::forward<Args>(args)...);
}

template<size_t N, typename... Args>
void format(static_string<N> &destination, string_view fmt, Args &&...args) {
  destination.clear();
  detail::append_format_sink<static_string<N>> target{destination};
  format_dst(target, fmt, kstd::forward<Args>(args)...);
}


}// namespace kstd
