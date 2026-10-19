#include <undoscrt/string.h>

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;

  for (size_t i = 0; i < n; i++)
    d[i] = s[i];

  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;

  if (d == s || n == 0)
    return dst;

  if (d < s) {
    // forward copy
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  } else {
    // backward copy
    for (size_t i = n; i != 0; i--)
      d[i - 1] = s[i - 1];
  }

  return dst;
}

void *memset(void *dst, int value, size_t n) {
  unsigned char *d = dst;
  unsigned char v = (unsigned char)value;

  for (size_t i = 0; i < n; i++)
    d[i] = v;

  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a;
  const unsigned char *y = b;

  for (size_t i = 0; i < n; i++) {
    if (x[i] != y[i])
      return (int)x[i] - (int)y[i];
  }

  return 0;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len]) ++len;
  return len;
}

int strcmp(const char *a, const char *b) {
  while (*a && (*a == *b)) {
    ++a;
    ++b;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  while (n && *a && (*a == *b)) {
    ++a;
    ++b;
    --n;
  }
  if (n == 0) return 0;
  return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
  char *out = dst;
  while ((*out++ = *src++) != '\0') {}
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  for (; i < n; ++i) {
    dst[i] = '\0';
  }
  return dst;
}

char *strcat(char *dst, const char *src) {
  char *out = dst + strlen(dst);
  while ((*out++ = *src++) != '\0') {}
  return dst;
}

char *strchr(const char *s, int c) {
  for (; *s; ++s) {
    if (*s == (char)c) return (char *)s;
  }
  return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
  const char *last = (c == '\0') ? s + strlen(s) : NULL;
  for (; *s; ++s) {
    if (*s == (char)c) last = s;
  }
  return (char *)last;
}
