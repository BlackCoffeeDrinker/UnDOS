#include <undoscrt/stdio.h>
#include <undoscrt/string.h>

#include "../userspace.h"
#include <undos/syscalls.h>

namespace {
// Big enough for one crt_printf call's worth of formatted output; longer
// output is simply truncated rather than growing dynamically, keeping this
// routine free of any heap dependency.
constexpr size_t OUTPUT_BUFFER_SIZE = 512;

// Renders `value` in `base` (10 or 16) into `out`, returns the number of
// characters written. `out` must have room for at least 32 characters.
size_t FormatUnsigned(uint32_t value, unsigned base, bool uppercase, char *out) {
  constexpr const char *kLowerDigits = "0123456789abcdef";
  constexpr const char *kUpperDigits = "0123456789ABCDEF";
  const char *digits = uppercase ? kUpperDigits : kLowerDigits;

  char scratch[32];
  size_t len = 0;
  do {
    scratch[len++] = digits[value % base];
    value /= base;
  } while (value != 0);

  for (size_t i = 0; i < len; ++i) {
    out[i] = scratch[len - 1 - i];
  }
  return len;
}

size_t FormatSigned(int32_t value, char *out) {
  if (value < 0) {
    out[0] = '-';
    return 1 + FormatUnsigned(static_cast<uint32_t>(-value), 10, false, out + 1);
  }
  return FormatUnsigned(static_cast<uint32_t>(value), 10, false, out);
}
}// namespace

extern "C" void crt_puts(const char *message) {
  (void) UD_SYS_PrintV1(message);
}

extern "C" void crt_vprintf(const char *fmt, va_list args) {
  char buffer[OUTPUT_BUFFER_SIZE];
  size_t pos = 0;

  const auto put_char = [&](char c) {
    if (pos < OUTPUT_BUFFER_SIZE - 1) buffer[pos++] = c;
  };
  const auto put_string = [&](const char *s) {
    while (*s) put_char(*s++);
  };

  for (const char *p = fmt; *p; ++p) {
    if (*p != '%') {
      put_char(*p);
      continue;
    }

    ++p;
    char digits[32];
    switch (*p) {
      case 's': {
        const char *s = va_arg(args, const char *);
        put_string(s ? s : "(null)");
        break;
      }
      case 'c':
        put_char(static_cast<char>(va_arg(args, int)));
        break;
      case 'd': {
        const size_t len = FormatSigned(va_arg(args, int32_t), digits);
        for (size_t i = 0; i < len; ++i) put_char(digits[i]);
        break;
      }
      case 'u': {
        const size_t len = FormatUnsigned(va_arg(args, uint32_t), 10, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(digits[i]);
        break;
      }
      case 'x': {
        const size_t len = FormatUnsigned(va_arg(args, uint32_t), 16, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(digits[i]);
        break;
      }
      case 'p': {
        put_string("0x");
        const size_t len = FormatUnsigned(reinterpret_cast<uintptr_t>(va_arg(args, void *)), 16, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(digits[i]);
        break;
      }
      case '%':
        put_char('%');
        break;
      case '\0':
        --p;// avoid stepping past the terminator in the for-loop increment
        break;
      default:
        put_char('%');
        put_char(*p);
        break;
    }
  }

  buffer[pos] = '\0';
  crt_puts(buffer);
}

extern "C" void crt_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  crt_vprintf(fmt, args);
  va_end(args);
}
