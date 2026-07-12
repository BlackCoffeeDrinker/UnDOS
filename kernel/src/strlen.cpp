
#include "stdkrn.hpp"

UNDOS_KERNEL_API_DEF int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = static_cast<const unsigned char *>(s1);
  const unsigned char *p2 = static_cast<const unsigned char *>(s2);
  for (size_t i = 0; i < n; i++) {
    if (p1[i] < p2[i]) return -1;
    if (p1[i] > p2[i]) return 1;
  }
  return 0;
}

UNDOS_KERNEL_API_DEF  size_t strlen(const char *str) {
  const char *s;

  for (s = str; *s; ++s)
    ;
  return static_cast<size_t>(s - str);
}
