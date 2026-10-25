#pragma once

#include <stddef.h>
#include <stdint.h>

namespace init {
enum class FontBackend : uint8_t { BuiltinBitmap, TrueType };

struct FontConfiguration {
  FontBackend backend{FontBackend::TrueType};
  char regular[256]{};
  char bold[256]{};
  uint32_t sizePt{};
  uint32_t ppem{};
};

namespace detail {
struct Slice {
  const char *data;
  size_t size;
};

inline bool horizontal_space(char ch) { return ch == ' ' || ch == '\t'; }

inline Slice trim(Slice value) {
  while (value.size && horizontal_space(value.data[0])) {
    ++value.data;
    --value.size;
  }
  while (value.size && horizontal_space(value.data[value.size - 1])) --value.size;
  return value;
}

inline bool equals(Slice value, const char *text) {
  size_t length = 0;
  while (text[length]) ++length;
  if (value.size != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (value.data[i] != text[i]) return false;
  return true;
}

inline bool parse_path(Slice value, char *output) {
  value = trim(value);
  if (!value.size) return false;
  bool quoted = value.data[0] == '"';
  if (quoted) {
    if (value.size < 2 || value.data[value.size - 1] != '"') return false;
    ++value.data;
    value.size -= 2;
  }
  if (!value.size || value.size > 255 || value.data[0] != '/') return false;
  for (size_t i = 0; i < value.size; ++i) {
    const auto ch = static_cast<uint8_t>(value.data[i]);
    if (ch < 0x20 || ch == 0x7f || ch == '"' || (!quoted && horizontal_space(value.data[i])))
      return false;
    output[i] = value.data[i];
  }
  output[value.size] = '\0';
  return true;
}

inline bool parse_size(Slice value, uint32_t &sizePt) {
  if (!value.size) return false;
  uint32_t parsed = 0;
  for (size_t i = 0; i < value.size; ++i) {
    const char ch = value.data[i];
    if (ch < '0' || ch > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(ch - '0');
    if (parsed > (192 - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  if (!parsed) return false;
  sizePt = parsed;
  return true;
}

inline bool append(char *output, size_t capacity, size_t &length, char value) {
  if (length + 1 >= capacity) return false;
  output[length++] = value;
  output[length] = '\0';
  return true;
}

inline bool append_text(char *output, size_t capacity, size_t &length, const char *text) {
  for (size_t i = 0; text[i]; ++i)
    if (!append(output, capacity, length, text[i])) return false;
  return true;
}

inline bool append_decimal(char *output, size_t capacity, size_t &length, uint32_t value) {
  char digits[10]{};
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value);
  while (count)
    if (!append(output, capacity, length, digits[--count])) return false;
  return true;
}

inline bool append_path(char *output, size_t capacity, size_t &length, const char *path) {
  size_t pathLength = 0;
  while (path[pathLength]) {
    const auto ch = static_cast<uint8_t>(path[pathLength]);
    if (pathLength == 255 || ch < 0x20 || ch >= 0x7f || ch == '"') return false;
    ++pathLength;
  }
  if (!pathLength || path[0] != '/' ||
      !append(output, capacity, length, '"')) return false;
  for (size_t i = 0; i < pathLength; ++i)
    if (!append(output, capacity, length, path[i])) return false;
  return append(output, capacity, length, '"');
}

} // namespace detail

inline bool point_size_to_ppem(uint32_t sizePt, uint32_t &ppem) {
  if (sizePt < 1 || sizePt > 192) return false;
  ppem = (sizePt * 96 + 36) / 72;
  return ppem >= 1 && ppem <= 256;
}

inline bool parse_font_config(const char *text, size_t length, FontConfiguration &out) {
  if (!text || !length || length > 1023) return false;
  for (size_t i = 0; i < length; ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);
    if (!ch || ch > 0x7f || (ch == '\r' && (i + 1 == length || text[i + 1] != '\n')))
      return false;
  }

  FontConfiguration parsed{};
  bool section = false;
  bool backend = false, regular = false, bold = false, size = false;
  size_t offset = 0;
  while (offset < length) {
    size_t end = offset;
    while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
    auto line = detail::trim({text + offset, end - offset});
    if (end < length && text[end] == '\r') end += 2;
    else if (end < length) ++end;
    offset = end;
    if (!line.size || line.data[0] == '#' || line.data[0] == ';') continue;

    if (line.data[0] == '[') {
      if (line.size < 2 || line.data[line.size - 1] != ']' ||
          !detail::equals(detail::trim({line.data + 1, line.size - 2}), "Fonts") || section)
        return false;
      section = true;
      continue;
    }
    if (!section) return false;

    size_t equals = 0;
    while (equals < line.size && line.data[equals] != '=') ++equals;
    if (equals == line.size) return false;
    const auto key = detail::trim({line.data, equals});
    const auto value = detail::trim({line.data + equals + 1, line.size - equals - 1});
    if (detail::equals(key, "Backend")) {
      if (backend) return false;
      if (detail::equals(value, "bitmap"))
        parsed.backend = FontBackend::BuiltinBitmap;
      else if (detail::equals(value, "truetype"))
        parsed.backend = FontBackend::TrueType;
      else
        return false;
      backend = true;
    } else if (detail::equals(key, "Regular")) {
      if (regular || !detail::parse_path(value, parsed.regular)) return false;
      regular = true;
    } else if (detail::equals(key, "Bold")) {
      if (bold || !detail::parse_path(value, parsed.bold)) return false;
      bold = true;
    } else if (detail::equals(key, "SizePt")) {
      if (size || !detail::parse_size(value, parsed.sizePt)) return false;
      size = true;
    } else {
      return false;
    }
  }
  if (!section || !backend) return false;
  if (parsed.backend == FontBackend::BuiltinBitmap) {
    if (regular || bold || size) return false;
  } else if (!regular || !bold || !size ||
             !point_size_to_ppem(parsed.sizePt, parsed.ppem)) {
    return false;
  }
  out = parsed;
  return true;
}

inline bool make_font_loader_arguments(const FontConfiguration &config, char *output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  uint32_t converted = 0;
  if (config.backend != FontBackend::TrueType ||
      !point_size_to_ppem(config.sizePt, converted) || converted != config.ppem) return false;
  size_t length = 0;
  if (!detail::append_path(output, capacity, length, config.regular) ||
      !detail::append(output, capacity, length, ' ') ||
      !detail::append_path(output, capacity, length, config.bold) ||
      !detail::append(output, capacity, length, ' ')) return false;
  char digits[3]{};
  size_t digitCount = 0;
  for (uint32_t value = config.ppem; value; value /= 10)
    digits[digitCount++] = static_cast<char>('0' + value % 10);
  while (digitCount)
    if (!detail::append(output, capacity, length, digits[--digitCount])) return false;
  return true;
}

inline bool make_font_config_diagnostic(const FontConfiguration &config, char *output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  size_t length = 0;
  if (config.backend == FontBackend::BuiltinBitmap)
    return detail::append_text(output, capacity, length,
                               "init: default font config backend=bitmap");
  uint32_t converted = 0;
  if (!point_size_to_ppem(config.sizePt, converted) || converted != config.ppem) return false;
  if (!detail::append_text(output, capacity, length, "init: default font config backend=truetype regular=") ||
      !detail::append_path(output, capacity, length, config.regular) ||
      !detail::append_text(output, capacity, length, " bold=") ||
      !detail::append_path(output, capacity, length, config.bold) ||
      !detail::append_text(output, capacity, length, " size=") ||
      !detail::append_decimal(output, capacity, length, config.sizePt) ||
      !detail::append_text(output, capacity, length, "pt ppem=") ||
      !detail::append_decimal(output, capacity, length, config.ppem)) return false;
  return true;
}
} // namespace init
