#pragma once

#include <strfmt.hpp>

/**
 * @brief Method KE_PrintBuffer
 *
 * Emits one kernel log line. The line is prefixed with the system time
 * (dmesg style, "[    5.951234] ") and terminated with CRLF, so callers must
 * not add their own line ending. Trailing CR/LF characters are trimmed and any
 * embedded line break starts a new, separately timestamped line.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_PrintBuffer, const char *str, size_t len);

// Every KE_Print call emits exactly one log line; do not end messages with
// "\r\n". Each overload hands the whole message to KE_PrintBuffer at once.

// 1. Fixed-size arrays & String Literals (Safe against non-NUL-padded buffers)
template<size_t N>
void KE_Print(const char (&str)[N]) noexcept {
  static_assert(N > 0, "String array cannot be empty");
  const kstd::string_view sv(str);
  KE_PrintBuffer(sv.data(), sv.size());
}

// 2. Handles kstd::string_view AND dynamic const char* (via implicit conversion)
inline void KE_Print(kstd::string_view sv) noexcept {
  KE_PrintBuffer(sv.data(), sv.size());
}

namespace kernel::detail {

struct PrintBufferSink {
  kstd::span<char> buffer;
  size_t size = 0;
  bool truncated = false;

  void operator()(char c) noexcept {
    if (size < buffer.size()) {
      buffer[size++] = c;
    } else {
      truncated = true;
    }
  }
};

}// namespace kernel::detail

// 3. Variadic Formatted KE_Print; this could be called before VMM & PMM is online.
// Output longer than BUFFER_SIZE is truncated so the message stays one line.
template<size_t BUFFER_SIZE = 256, typename... Args>
  requires(sizeof...(Args) > 0)
void KE_Print(kstd::string_view fmt, Args &&...args) noexcept {
  static_assert(BUFFER_SIZE > 3, "BUFFER_SIZE must be greater than 3.");

  char buffer[BUFFER_SIZE];
  kernel::detail::PrintBufferSink sink{kstd::span<char>(buffer)};
  const auto fmt_args = kstd::detail::make_args(args...);
  kstd::detail::format_args_to(sink, fmt, kstd::span<const kstd::detail::format_arg>(fmt_args));

  if (sink.truncated) {
    buffer[BUFFER_SIZE - 3] = '.';
    buffer[BUFFER_SIZE - 2] = '.';
    buffer[BUFFER_SIZE - 1] = '.';
  }
  KE_PrintBuffer(buffer, sink.size);
}
