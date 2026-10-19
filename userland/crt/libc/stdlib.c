#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <undoscrt/heap.h>

static int error_number;

static bool space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
static unsigned digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return 36;
}
static unsigned long convert(const char *text, char **end, int base, bool signed_value,
                             bool *negative, bool *overflow) {
  const char *start = text;
  if (end) *end = (char *)start;
  if (base && (base < 2 || base > 36)) { errno = EINVAL; return 0; }
  while (space(*text)) ++text;
  *negative = *text == '-';
  if (*text == '+' || *text == '-') ++text;
  if ((!base || base == 16) && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X') && digit(text[2]) < 16) {
    base = 16;
    text += 2;
  }
  if (!base) base = *text == '0' ? 8 : 10;
  const unsigned long limit = signed_value
      ? (unsigned long)LONG_MAX + (*negative ? 1ul : 0ul) : ULONG_MAX;
  unsigned long value = 0;
  const char *digits = text;
  while (digit(*text) < (unsigned)base) {
    const unsigned d = digit(*text++);
    if (value > (limit - d) / (unsigned)base) *overflow = true;
    else if (!*overflow) value = value * (unsigned)base + d;
  }
  if (text == digits) return 0;
  if (end) *end = (char *)text;
  if (*overflow) { errno = ERANGE; return limit; }
  return value;
}
int *__undos_errno(void) { return &error_number; }
void *malloc(size_t size) {
  void *result = crt_malloc(size);
  if (!result && size) errno = ENOMEM;
  return result;
}
void *calloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) { errno = ENOMEM; return NULL; }
  void *result = crt_calloc(count, size);
  if (!result && count && size) errno = ENOMEM;
  return result;
}
void *realloc(void *memory, size_t size) {
  void *result = crt_realloc(memory, size);
  if (!result && size) errno = ENOMEM;
  return result;
}
void free(void *memory) { crt_free(memory); }
int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }
long strtol(const char *text, char **end, int base) {
  bool negative = false;
  bool overflow = false;
  const unsigned long value = convert(text, end, base, true, &negative, &overflow);
  if (negative) return value == (unsigned long)LONG_MAX + 1ul
      ? LONG_MIN : -(long)value;
  return (long)value;
}
unsigned long strtoul(const char *text, char **end, int base) {
  bool negative = false;
  bool overflow = false;
  const unsigned long value = convert(text, end, base, false, &negative, &overflow);
  // Overflow must saturate, even for a negative input.
  if (overflow) return value;
  return negative ? 0ul - value : value;
}
int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
long atol(const char *text) { return strtol(text, NULL, 10); }
char *getenv(const char *name) {
  if (!name || !*name) { errno = EINVAL; return NULL; }
  for (const char *p = name; *p; ++p) if (*p == '=') { errno = EINVAL; return NULL; }
  return NULL; // ABI v1 has an empty environment.
}
int system(const char *command) {
  if (!command) return 0;
  errno = ENOSYS;
  return -1;
}
_Noreturn void abort(void) { _Exit(EXIT_FAILURE); }
