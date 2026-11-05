#include "private/utf8.h"

#include <stdint.h>

int undos_utf8_valid(const char *text, size_t bytes) {
  const uint8_t *input = (const uint8_t *)text;
  size_t offset = 0;
  while (offset < bytes) {
    uint8_t first = input[offset++];
    if (first <= 0x7f) continue;

    uint32_t scalar;
    uint32_t remaining;
    if (first >= 0xc2 && first <= 0xdf) {
      scalar = first & 0x1fu;
      remaining = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      scalar = first & 0x0fu;
      remaining = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      scalar = first & 0x07u;
      remaining = 3;
    } else {
      return 0;
    }
    if (remaining > bytes - offset) return 0;
    for (uint32_t i = 0; i < remaining; ++i) {
      uint8_t next = input[offset++];
      if ((next & 0xc0u) != 0x80u) return 0;
      if (i == 0 &&
          ((first == 0xe0 && next < 0xa0) ||
           (first == 0xed && next >= 0xa0) ||
           (first == 0xf0 && next < 0x90) ||
           (first == 0xf4 && next > 0x8f)))
        return 0;
      scalar = (scalar << 6) | (next & 0x3fu);
    }
    if (scalar > 0x10ffffu || (scalar >= 0xd800u && scalar <= 0xdfffu))
      return 0;
  }
  return 1;
}
