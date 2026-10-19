#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
  char *buffer;
  size_t capacity;
  FILE *stream;
  int count;
  bool failed;
  char pending[128];
  size_t used;
} Output;

static void output_flush(Output *out) {
  if (out->used && fwrite(out->pending, 1, out->used, out->stream) != out->used)
    out->failed = true;
  out->used = 0;
}
static void output_put(Output *out, char ch) {
  if (out->failed) return;
  if (out->count == INT_MAX) { errno = EOVERFLOW; out->failed = true; return; }
  if (out->stream) {
    out->pending[out->used++] = ch;
    if (out->used == sizeof(out->pending)) output_flush(out);
  } else if (out->capacity && (size_t)out->count < out->capacity - 1)
    out->buffer[out->count] = ch;
  ++out->count;
}
static void output_repeat(Output *out, char ch, int size) {
  while (size-- > 0 && !out->failed) output_put(out, ch);
}
typedef enum { Normal, HH, H, L, LL, Z, T, J } Length;

static int number(const char **text, Output *out) {
  int value = 0;
  while (**text >= '0' && **text <= '9') {
    const int digit = *(*text)++ - '0';
    if (value > (INT_MAX - digit) / 10) {
      errno = EOVERFLOW;
      out->failed = true;
      return 0;
    }
    value = value * 10 + digit;
  }
  return value;
}

static int format(Output *out, const char *text, va_list args) {
  if (!text) { errno = EINVAL; return -1; }
  while (*text && !out->failed) {
    if (*text != '%') { output_put(out, *text++); continue; }
    ++text;
    bool left = false, plus = false, blank = false, alternate = false, zero = false;
    bool flags = true;
    while (flags) {
      switch (*text) {
        case '-': left = true; ++text; break;
        case '+': plus = true; ++text; break;
        case ' ': blank = true; ++text; break;
        case '#': alternate = true; ++text; break;
        case '0': zero = true; ++text; break;
        default: flags = false; break;
      }
    }
    int width;
    if (*text == '*') {
      ++text;
      width = va_arg(args, int);
      if (width == INT_MIN) { errno = EOVERFLOW; out->failed = true; break; }
      if (width < 0) { left = true; width = -width; }
    } else width = number(&text, out);
    int precision = -1;
    if (*text == '.') {
      ++text;
      if (*text == '*') { ++text; precision = va_arg(args, int); }
      else precision = number(&text, out);
      if (precision < 0) precision = -1;
    }
    Length length = Normal;
    if (*text == 'h') {
      ++text;
      if (*text == 'h') { ++text; length = HH; }
      else length = H;
    } else if (*text == 'l') {
      ++text;
      if (*text == 'l') { ++text; length = LL; }
      else length = L;
    } else if (*text == 'z') { ++text; length = Z; }
    else if (*text == 't') { ++text; length = T; }
    else if (*text == 'j') { ++text; length = J; }
    const char spec = *text;
    if (spec) ++text;
    if (out->failed) break;
    if (spec == 's' || spec == 'c' || spec == '%') {
      if (length != Normal) { errno = ENOSYS; out->failed = true; break; }
      char ch = spec == 'c' ? (char)va_arg(args, int) : '%';
      const char *value = spec == 's' ? va_arg(args, const char *) : &ch;
      if (!value) { errno = EINVAL; out->failed = true; break; }
      int size = 0;
      if (spec == 's') {
        while ((precision < 0 || size < precision) && value[size]) {
          if (size == INT_MAX) { errno = EOVERFLOW; out->failed = true; break; }
          ++size;
        }
      } else size = 1;
      if (!left) output_repeat(out, ' ', width - size);
      for (int i = 0; i < size && !out->failed; ++i) output_put(out, value[i]);
      if (left) output_repeat(out, ' ', width - size);
      continue;
    }
    if (!spec || !strchr("diuoxXp", spec)) {
      errno = spec && strchr("aAeEfFgGn", spec) ? ENOSYS : EINVAL;
      out->failed = true;
      break;
    }
    const bool signed_value = spec == 'd' || spec == 'i';
    uint64_t value = 0;
    char sign = 0;
    if (spec == 'p') {
      if (length != Normal) { errno = EINVAL; out->failed = true; break; }
      value = (uintptr_t)va_arg(args, void *);
    } else if (signed_value) {
      int64_t v;
      switch (length) {
        case LL: v = va_arg(args, long long); break;
        case L: v = va_arg(args, long); break;
        case Z: case T: v = va_arg(args, ptrdiff_t); break;
        case J: v = va_arg(args, intmax_t); break;
        case H: v = (short)va_arg(args, int); break;
        case HH: v = (signed char)va_arg(args, int); break;
        default: v = va_arg(args, int); break;
      }
      sign = v < 0 ? '-' : plus ? '+' : blank ? ' ' : 0;
      value = v < 0 ? 0ull - (uint64_t)v : (uint64_t)v;
    } else {
      switch (length) {
        case LL: value = va_arg(args, unsigned long long); break;
        case L: value = va_arg(args, unsigned long); break;
        case Z: case T: value = va_arg(args, size_t); break;
        case J: value = va_arg(args, uintmax_t); break;
        case H: value = (unsigned short)va_arg(args, int); break;
        case HH: value = (unsigned char)va_arg(args, int); break;
        default: value = va_arg(args, unsigned); break;
      }
    }
    const unsigned base = spec == 'o' ? 8 : (spec == 'x' || spec == 'X' || spec == 'p') ? 16 : 10;
    const char *digits = spec == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[64];
    int size = 0;
    const uint64_t original = value;
    if (value || precision != 0 || spec == 'p') {
      do { reversed[size++] = digits[value % base]; value /= base; } while (value);
    }
    const bool hex_prefix = spec == 'p' || (alternate && base == 16 && original);
    if (alternate && base == 8 && (!size || reversed[size - 1] != '0') && precision <= size)
      precision = size + 1;
    int zeros = precision > size ? precision - size : 0;
    const int prefix_size = (sign ? 1 : 0) + (hex_prefix ? 2 : 0);
    if (size > INT_MAX - prefix_size - zeros) {
      errno = EOVERFLOW; out->failed = true; break;
    }
    int padding = width - size - zeros - prefix_size;
    if (zero && !left && precision < 0 && padding > 0) { zeros += padding; padding = 0; }
    if (!left) output_repeat(out, ' ', padding);
    if (sign) output_put(out, sign);
    if (hex_prefix) { output_put(out, '0'); output_put(out, spec == 'X' ? 'X' : 'x'); }
    output_repeat(out, '0', zeros);
    while (size && !out->failed) output_put(out, reversed[--size]);
    if (left) output_repeat(out, ' ', padding);
  }
  if (out->stream) output_flush(out);
  else if (out->capacity) {
    const size_t end = (size_t)out->count < out->capacity
        ? (size_t)out->count : out->capacity - 1;
    out->buffer[end] = '\0';
  }
  return out->failed ? -1 : out->count;
}

int vsnprintf(char *buffer, size_t size, const char *text, va_list args) {
  if (size && !buffer) { errno = EINVAL; return -1; }
  Output out = {.buffer = buffer, .capacity = size};
  return format(&out, text, args);
}
int snprintf(char *buffer, size_t size, const char *text, ...) {
  va_list args;
  va_start(args, text);
  const int result = vsnprintf(buffer, size, text, args);
  va_end(args);
  return result;
}
int vfprintf(FILE *stream, const char *text, va_list args) {
  if (!stream) { errno = EBADF; return -1; }
  Output out = {.stream = stream};
  return format(&out, text, args);
}
int fprintf(FILE *stream, const char *text, ...) {
  va_list args;
  va_start(args, text);
  const int result = vfprintf(stream, text, args);
  va_end(args);
  return result;
}
int vprintf(const char *text, va_list args) { return vfprintf(stdout, text, args); }
int printf(const char *text, ...) {
  va_list args;
  va_start(args, text);
  const int result = vprintf(text, args);
  va_end(args);
  return result;
}
