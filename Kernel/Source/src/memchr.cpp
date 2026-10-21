
#include "stdkrn.hpp"

extern "C" {
void *memchr(const void *bigptr, int ch, size_t length) {
  const auto big = static_cast<const char *>(bigptr);
  size_t n = 0;

  for (n = 0; n < length; n++)
    if (big[n] == ch)
      return const_cast<void *>(static_cast<const void *>(&big[n]));

  return nullptr;
}
}
