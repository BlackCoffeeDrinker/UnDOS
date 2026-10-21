#pragma once

#include <stddef.h>
#include <stdint.h>

namespace undos::font {

// UFT1 is a little-endian wire format, never an in-memory C++ object dump.
constexpr uint32_t Magic = 0x31544655;
constexpr uint32_t Version = 1;
constexpr uint32_t HeaderSize = 64;
constexpr uint32_t DirectoryEntrySize = 12;
constexpr uint32_t SectionCount = 10;
constexpr uint32_t PayloadStart = HeaderSize + DirectoryEntrySize * SectionCount;
constexpr uint32_t MaxFileBytes = 4 * 1024 * 1024;
constexpr uint32_t MaxResourceBytes = 8 * 1024 * 1024;
constexpr uint32_t MaxGlyphs = 4096;
constexpr uint32_t MaxProgramBytes = 65536;
constexpr uint32_t MaxPointsPerGlyph = 16384;
constexpr uint32_t MaxContoursPerGlyph = 2048;
constexpr uint32_t MaxComponentsPerGlyph = 64;
constexpr uint32_t MaxComponentDepth = 32;
constexpr uint32_t MaxGraphWork = 1024 * 1024;
constexpr uint32_t GlyphSize = 60;
constexpr uint32_t PointSize = 12;
constexpr uint32_t ComponentSize = 32;
constexpr uint32_t CmapSize = 12;
constexpr uint32_t LimitsSize = 52;

enum class Section : uint32_t {
  Glyphs = 1, Points, Contours, Components, Cmap, Cvt,
  GlyphPrograms, FontProgram, PrepProgram, Limits
};

// Header: magic/version/header-size/total-size/section-count/glyph-count/
// units-per-em/ascent/descent/line-gap/weight/style-flags, then four zero words.
// Style bit 0 = italic, bit 1 = fixed-pitch. Names are deliberately not identity.
// Glyph: kind(0 absent,1 simple,2 composite), advance, lsb, xmin/ymin/xmax/ymax,
// first-point/count, first-contour/count, first-component/count, program-offset/size.
// Point: original signed x/y and on-curve(0/1). No implied or phantom points.
// Contours: glyph-local inclusive endpoint indices.
// Component: glyph-id, original flags, signed arg1/arg2, xx/xy/yx/yy in 2.14.
// Cmap: first-codepoint, last-codepoint, first-glyph; inclusive sequential ranges.
// CVT: signed int32 font units. Programs retain original instruction bytes.
// Limits: maxp v1's thirteen uint16 fields widened to uint32 in original order.

struct Bytes {
  const uint8_t *data{nullptr};
  size_t size{0};
};

inline uint32_t read_u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t read_i32(const uint8_t *p) {
  const uint32_t value = read_u32(p);
  return value <= INT32_MAX ? static_cast<int32_t>(value) :
         -1 - static_cast<int32_t>(UINT32_MAX - value);
}

inline void write_u32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint32_t aligned(uint32_t value) { return (value + 3u) & ~3u; }

enum class Error : uint32_t {
  None, InvalidSize, InvalidHeader, UnsupportedVersion, InvalidDirectory,
  InvalidMetrics, InvalidGlyph, InvalidPoint, InvalidContour, InvalidComponent,
  InvalidCmap, InvalidProgram, InvalidLimits, ResourceLimit
};

const char *error_message(Error error);

// Returned views borrow validated immutable bytes; no API accepts a trusted flag.
class ResourceView {
public:
  explicit ResourceView(Bytes bytes) : bytes_(bytes) {}
  Bytes bytes() const { return bytes_; }
  Bytes section(Section id) const {
    const auto index = static_cast<uint32_t>(id) - 1;
    if (!bytes_.data || index >= SectionCount || bytes_.size < PayloadStart) return {};
    const auto *entry = bytes_.data + HeaderSize + index * DirectoryEntrySize;
    const uint32_t offset = read_u32(entry + 4), size = read_u32(entry + 8);
    if (offset > bytes_.size || size > bytes_.size - offset) return {};
    return {.data = bytes_.data + offset, .size = size};
  }
private:
  Bytes bytes_;
};

// Structural validation only. Acceptance does not claim VM/raster capabilities.
Error validate(Bytes bytes);

} // namespace undos::font
