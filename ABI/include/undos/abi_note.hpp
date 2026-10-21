#pragma once

#include <undos/abi.h>

namespace undos::abi {
inline uint32_t read_word(const uint8_t *bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
         (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

// Unknown notes are permitted; malformed or incompatible UnDOS notes are not.
inline bool scan_notes(const uint8_t *bytes, uint32_t size, uint32_t &matches) {
  while (size) {
    if (size < 12) return false;
    const auto name_size = read_word(bytes);
    const auto descriptor_size = read_word(bytes + 4);
    const auto type = read_word(bytes + 8);
    bytes += 12;
    size -= 12;
    if (name_size > size || name_size > UINT32_MAX - 3) return false;
    const auto name_span = (name_size + 3) & ~uint32_t(3);
    if (name_span > size || descriptor_size > size - name_span ||
        descriptor_size > UINT32_MAX - 3) return false;
    const auto descriptor_span = (descriptor_size + 3) & ~uint32_t(3);
    if (descriptor_span > size - name_span) return false;
    if (name_size == 6 && bytes[0] == 'U' && bytes[1] == 'n' &&
        bytes[2] == 'D' && bytes[3] == 'O' && bytes[4] == 'S' && !bytes[5]) {
      if (type != UNDOS_ABI_NOTE_TYPE || descriptor_size != 8 ||
          read_word(bytes + name_span) != UNDOS_APPLICATION_ABI_VERSION ||
          read_word(bytes + name_span + 4) != 0 || ++matches != 1) return false;
    }
    bytes += name_span + descriptor_span;
    size -= name_span + descriptor_span;
  }
  return true;
}
}
