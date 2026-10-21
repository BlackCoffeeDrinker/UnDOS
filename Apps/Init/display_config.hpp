#pragma once

#include <stddef.h>
#include <stdint.h>

namespace init {
struct DisplayPreference {
  uint32_t width{}, height{}, bpp{}, refreshHz{};
};

inline bool parse_display_config(const char *text, size_t length, DisplayPreference &out) {
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  uint32_t values[4]{};
  size_t offset = 0;
  for (auto &value : values) {
    while (offset < length && space(text[offset])) ++offset;
    if (offset == length || text[offset] < '0' || text[offset] > '9') return false;
    while (offset < length && text[offset] >= '0' && text[offset] <= '9') {
      const uint32_t digit = static_cast<uint32_t>(text[offset++] - '0');
      if (value > (UINT32_MAX - digit) / 10) return false;
      value = value * 10 + digit;
    }
    if (offset < length && !space(text[offset])) return false;
  }
  while (offset < length && space(text[offset])) ++offset;
  if (offset != length || !values[0] || !values[1] || values[0] > 32767 ||
      values[1] > 32767 || !values[2] || values[2] > 32) return false;
  out = {.width = values[0], .height = values[1], .bpp = values[2], .refreshHz = values[3]};
  return true;
}
}
