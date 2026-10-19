#include "decoder.hpp"

namespace undos::font::loader {
namespace {
using Buffer = kstd::vector<uint8_t>;
constexpr uint32_t tag(char a, char b, char c, char d) {
  return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
}
uint16_t u16(const uint8_t *p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
int32_t s16(const uint8_t *p) {
  const uint32_t v = u16(p);
  return v < 32768 ? int32_t(v) : int32_t(v) - 65536;
}
uint32_t u32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
bool has(Bytes b, size_t offset, size_t size) {
  return offset <= b.size && size <= b.size - offset;
}
struct Cursor {
  Bytes bytes;
  size_t at{0};
  bool take(size_t count, const uint8_t *&out) {
    if (!has(bytes, at, count)) return false;
    out = bytes.data + at;
    at += count;
    return true;
  }
};
struct Glyph {
  uint32_t kind{0}, advance{0};
  int32_t lsb{0}, bounds[4]{};
  uint32_t point{0}, points{0}, contour{0}, contours{0};
  uint32_t component{0}, components{0}, program{0}, programs{0};
};
struct Component {
  uint32_t glyph, flags;
  int32_t args[2], matrix[4];
};
struct Expansion {
  uint32_t points{0}, contours{0}, height{0}, state{0};
};

class Decoder {
public:
  explicit Decoder(Bytes bytes) : file(bytes) {}
  DecodeError run(Buffer &out);
private:
  Bytes file, head{}, maxp{}, hhea{}, hmtx{}, loca{}, glyf{}, cmap{}, os2{}, post{};
  Bytes fpgm{}, prep{}, cvt{};
  uint32_t glyphCount{0}, total{PayloadStart}, graphWork{0}, limits[13]{};
  kstd::vector<Glyph> glyphs;
  kstd::vector<Component> components;
  kstd::vector<Expansion> expansions;
  Buffer sections[SectionCount];
  DecodeError directory();
  DecodeError glyph(uint32_t index, Bytes bytes);
  DecodeError simple(Glyph &g, Cursor &cursor);
  DecodeError composite(Glyph &g, Cursor &cursor);
  DecodeError map();
  DecodeError map4(Bytes bytes);
  DecodeError map12(Bytes bytes);
  DecodeError visit(uint32_t index, uint32_t depth);
  bool room(uint32_t bytes) {
    if (bytes > MaxResourceBytes - total) return false;
    total += bytes;
    return true;
  }
  void word(Buffer &b, uint32_t v) {
    const auto at = b.size();
    b.resize(at + 4);
    write_u32(b.data() + at, v);
  }
  bool copy(Buffer &b, Bytes bytes) {
    if (bytes.size > MaxProgramBytes || !room(uint32_t(bytes.size))) return false;
    for (size_t i = 0; i < bytes.size; ++i) b.push_back(bytes.data[i]);
    return true;
  }
  bool mapping(uint32_t first, uint32_t last, uint32_t glyph);
};

DecodeError Decoder::directory() {
  if (!file.data || file.size < 12 || file.size > MaxFileBytes) return DecodeError::InvalidSize;
  if (u32(file.data) != 0x00010000 && u32(file.data) != tag('t','r','u','e'))
    return DecodeError::UnsupportedFormat;
  const uint32_t count = u16(file.data + 4);
  if (!count || count > 128 || !has(file, 12, count * 16)) return DecodeError::InvalidDirectory;
  const uint32_t directoryEnd = 12 + count * 16;
  for (uint32_t i = 0; i < count; ++i) {
    const auto *entry = file.data + 12 + i * 16;
    const uint32_t id = u32(entry), offset = u32(entry + 8), size = u32(entry + 12);
    if ((offset & 3) || offset < directoryEnd || !has(file, offset, size))
      return DecodeError::InvalidDirectory;
    for (uint32_t j = 0; j < i; ++j) {
      const auto *previous = file.data + 12 + j * 16;
      const uint32_t begin = u32(previous + 8), length = u32(previous + 12);
      if (id == u32(previous) || (size && length && offset < begin + length && begin < offset + size))
        return DecodeError::InvalidDirectory;
    }
    Bytes b{file.data + offset, size};
    switch (id) {
      case tag('h','e','a','d'): head = b; break;
      case tag('m','a','x','p'): maxp = b; break;
      case tag('h','h','e','a'): hhea = b; break;
      case tag('h','m','t','x'): hmtx = b; break;
      case tag('l','o','c','a'): loca = b; break;
      case tag('g','l','y','f'): glyf = b; break;
      case tag('c','m','a','p'): cmap = b; break;
      case tag('O','S','/','2'): os2 = b; break;
      case tag('p','o','s','t'): post = b; break;
      case tag('f','p','g','m'): fpgm = b; break;
      case tag('p','r','e','p'): prep = b; break;
      case tag('c','v','t',' '): cvt = b; break;
      case tag('C','F','F',' '): case tag('C','F','F','2'):
      case tag('f','v','a','r'): case tag('g','v','a','r'):
      case tag('a','v','a','r'): case tag('c','v','a','r'):
      case tag('H','V','A','R'): case tag('V','V','A','R'): case tag('M','V','A','R'):
      case tag('S','V','G',' '): case tag('C','O','L','R'):
      case tag('C','P','A','L'): case tag('s','b','i','x'):
      case tag('C','B','D','T'): case tag('C','B','L','C'):
      case tag('E','B','D','T'): case tag('E','B','L','C'):
        return DecodeError::UnsupportedFormat;
      default: break; // Unused layout/name/checksum tables are not rendering inputs.
    }
  }
  if (!head.data || !maxp.data || !hhea.data || !hmtx.data || !loca.data || !glyf.data || !cmap.data)
    return DecodeError::MissingTable;
  return DecodeError::None;
}

DecodeError Decoder::simple(Glyph &g, Cursor &cursor) {
  const uint8_t *p;
  if (g.contours > MaxContoursPerGlyph || g.contours > limits[1])
    return DecodeError::ResourceLimit;
  uint32_t previous = 0;
  for (uint32_t i = 0; i < g.contours; ++i) {
    if (!cursor.take(2, p)) return DecodeError::InvalidGlyph;
    const uint32_t endpoint = u16(p);
    if ((i && endpoint <= previous) || endpoint >= MaxPointsPerGlyph) return DecodeError::InvalidGlyph;
    if (!room(4)) return DecodeError::ResourceLimit;
    word(sections[2], endpoint);
    previous = endpoint;
  }
  g.points = g.contours ? previous + 1 : 0;
  if (g.points > limits[0]) return DecodeError::InvalidLimits;
  if (!cursor.take(2, p)) return DecodeError::InvalidGlyph;
  g.programs = u16(p);
  if (g.programs > limits[10] || !cursor.take(g.programs, p)) return DecodeError::InvalidGlyph;
  if (!copy(sections[6], {p, g.programs})) return DecodeError::ResourceLimit;
  Buffer flags;
  flags.reserve(g.points);
  while (flags.size() < g.points) {
    if (!cursor.take(1, p)) return DecodeError::InvalidGlyph;
    const uint8_t flag = p[0];
    if (flag & 0x80) return DecodeError::InvalidFlags;
    uint32_t repeat = 0;
    if (flag & 8) {
      if (!cursor.take(1, p)) return DecodeError::InvalidGlyph;
      repeat = p[0];
    }
    if (repeat >= g.points - flags.size()) return DecodeError::InvalidFlags;
    do { flags.push_back(flag); } while (repeat--);
  }
  if (!room(g.points * PointSize)) return DecodeError::ResourceLimit;
  auto &points = sections[1];
  const size_t start = points.size();
  points.resize(start + g.points * PointSize);
  for (uint32_t axis = 0; axis < 2; ++axis) {
    int32_t value = 0;
    for (uint32_t i = 0; i < g.points; ++i) {
      const uint8_t flag = flags[i];
      const uint32_t shortBit = axis ? 4 : 2, sameBit = axis ? 32 : 16;
      int32_t delta = 0;
      if (flag & shortBit) {
        if (!cursor.take(1, p)) return DecodeError::InvalidGlyph;
        delta = (flag & sameBit) ? int32_t(p[0]) : -int32_t(p[0]);
      } else if (!(flag & sameBit)) {
        if (!cursor.take(2, p)) return DecodeError::InvalidGlyph;
        delta = s16(p);
      }
      value += delta;
      if (value < -32768 || value > 32767 || value < g.bounds[axis] || value > g.bounds[axis + 2])
        return DecodeError::InvalidCoordinates;
      write_u32(points.data() + start + i * PointSize + axis * 4, uint32_t(value));
      if (!axis) write_u32(points.data() + start + i * PointSize + 8, flag & 1);
    }
  }
  return DecodeError::None;
}

DecodeError Decoder::composite(Glyph &g, Cursor &cursor) {
  const uint8_t *p;
  uint32_t flags = 0, metrics = 0;
  do {
    if (++g.components > MaxComponentsPerGlyph || g.components > limits[11])
      return DecodeError::ResourceLimit;
    if (!cursor.take(4, p)) return DecodeError::InvalidComponent;
    flags = u16(p);
    Component c{};
    c.flags = flags;
    c.glyph = u16(p + 2);
    if (c.glyph >= glyphCount || (flags & ~0x1fefu) ||
        ((flags & 0x1800) == 0x1800) || ((flags & 0x100) && (flags & 0x20)))
      return DecodeError::InvalidFlags;
    if ((flags & 0x200) && ++metrics > 1) return DecodeError::InvalidFlags;
    const uint32_t transforms = ((flags & 8) ? 1 : 0) + ((flags & 0x40) ? 1 : 0) + ((flags & 0x80) ? 1 : 0);
    if (transforms > 1) return DecodeError::InvalidFlags;
    if (!cursor.take((flags & 1) ? 4 : 2, p)) return DecodeError::InvalidComponent;
    for (uint32_t i = 0; i < 2; ++i) {
      if (flags & 1) c.args[i] = (flags & 2) ? s16(p + i * 2) : int32_t(u16(p + i * 2));
      else c.args[i] = (flags & 2) && p[i] >= 128 ? int32_t(p[i]) - 256 : int32_t(p[i]);
    }
    c.matrix[0] = c.matrix[3] = 16384;
    if (flags & 8) {
      if (!cursor.take(2, p)) return DecodeError::InvalidComponent;
      c.matrix[0] = c.matrix[3] = s16(p);
    } else if (flags & 0x40) {
      if (!cursor.take(4, p)) return DecodeError::InvalidComponent;
      c.matrix[0] = s16(p); c.matrix[3] = s16(p + 2);
    } else if (flags & 0x80) {
      if (!cursor.take(8, p)) return DecodeError::InvalidComponent;
      // SFNT stores xscale, scale01, scale10, yscale in this order.
      for (uint32_t i = 0; i < 4; ++i) c.matrix[i] = s16(p + i * 2);
    }
    if (!room(ComponentSize)) return DecodeError::ResourceLimit;
    components.push_back(c);
    auto &out = sections[3];
    word(out, c.glyph); word(out, c.flags);
    for (const auto arg : c.args) word(out, uint32_t(arg));
    for (const auto entry : c.matrix) word(out, uint32_t(entry));
  } while (flags & 0x20);
  if (flags & 0x100) {
    if (!cursor.take(2, p)) return DecodeError::InvalidGlyph;
    g.programs = u16(p);
    if (g.programs > limits[10] || !cursor.take(g.programs, p)) return DecodeError::InvalidGlyph;
    if (!copy(sections[6], {p, g.programs})) return DecodeError::ResourceLimit;
  }
  return DecodeError::None;
}

DecodeError Decoder::glyph(uint32_t index, Bytes bytes) {
  auto &g = glyphs[index];
  g.point = uint32_t(sections[1].size() / PointSize);
  g.contour = uint32_t(sections[2].size() / 4);
  g.component = uint32_t(components.size());
  g.program = uint32_t(sections[6].size());
  if (!bytes.size) return DecodeError::None;
  if (bytes.size < 10) return DecodeError::InvalidGlyph;
  const int32_t contours = s16(bytes.data);
  if (contours < -1) return DecodeError::InvalidGlyph;
  for (uint32_t i = 0; i < 4; ++i) g.bounds[i] = s16(bytes.data + 2 + i * 2);
  if (g.bounds[0] > g.bounds[2] || g.bounds[1] > g.bounds[3]) return DecodeError::InvalidGlyph;
  Cursor cursor{bytes, 10};
  g.kind = contours < 0 ? 2 : 1;
  g.contours = contours < 0 ? 0 : uint32_t(contours);
  const DecodeError error = contours < 0 ? composite(g, cursor) : simple(g, cursor);
  if (error != DecodeError::None) return error;
  // loca may account for alignment padding, but not unparsed glyph data.
  if (bytes.size - cursor.at > 3) return DecodeError::InvalidGlyph;
  while (cursor.at < bytes.size) if (bytes.data[cursor.at++] != 0) return DecodeError::InvalidGlyph;
  return DecodeError::None;
}

bool Decoder::mapping(uint32_t first, uint32_t last, uint32_t glyph) {
  if (first > last || last > 0x10ffff || (first <= 0xdfff && last >= 0xd800) ||
      glyph >= glyphCount || last - first >= glyphCount - glyph) return false;
  if (!glyph) {
    if (first == last) return true;
    ++first; ++glyph;
  }
  auto &b = sections[4];
  if (b.size() >= CmapSize) {
    auto *previous = b.data() + b.size() - CmapSize;
    if (read_u32(previous + 4) >= first) return false;
    if (read_u32(previous + 4) + 1 == first &&
        read_u32(previous + 8) + first - read_u32(previous) == glyph) {
      write_u32(previous + 4, last);
      return true;
    }
  }
  if (!room(CmapSize)) return false;
  word(b, first); word(b, last); word(b, glyph);
  return true;
}

DecodeError Decoder::map12(Bytes b) {
  if (b.size < 16 || u16(b.data + 2) || u32(b.data + 4) != b.size)
    return DecodeError::InvalidCmap;
  const uint32_t count = u32(b.data + 12);
  if (count > (b.size - 16) / 12 || b.size != 16 + count * 12)
    return DecodeError::InvalidCmap;
  uint32_t previous = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto *p = b.data + 16 + i * 12;
    const uint32_t first = u32(p), last = u32(p + 4), glyph = u32(p + 8);
    if ((i && first <= previous) || !mapping(first, last, glyph)) return DecodeError::InvalidCmap;
    previous = last;
  }
  return DecodeError::None;
}

DecodeError Decoder::map4(Bytes b) {
  if (b.size < 24 || u16(b.data + 2) != b.size) return DecodeError::InvalidCmap;
  const uint32_t count = u16(b.data + 6) / 2;
  if (!count || (u16(b.data + 6) & 1) || !has(b, 14, 8 * count + 2))
    return DecodeError::InvalidCmap;
  const uint32_t ends = 14, starts = 16 + count * 2, deltas = starts + count * 2;
  const uint32_t offsets = deltas + count * 2, array = offsets + count * 2;
  if (u16(b.data + 14 + count * 2)) return DecodeError::InvalidCmap;
  uint32_t previous = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t first = u16(b.data + starts + i * 2), last = u16(b.data + ends + i * 2);
    const uint32_t delta = u16(b.data + deltas + i * 2), offset = u16(b.data + offsets + i * 2);
    if (first > last || (i && first <= previous) || (offset & 1)) return DecodeError::InvalidCmap;
    if (i + 1 == count && (first != 0xffff || last != 0xffff)) return DecodeError::InvalidCmap;
    for (uint32_t code = first; code <= last; ++code) {
      uint32_t glyph = (code + delta) & 0xffff;
      if (offset) {
        const uint32_t at = offsets + i * 2 + offset + (code - first) * 2;
        if (at < array || !has(b, at, 2)) return DecodeError::InvalidCmap;
        glyph = u16(b.data + at);
        if (glyph) glyph = (glyph + delta) & 0xffff;
      }
      if (!glyph) continue;
      if (!mapping(code, code, glyph)) return DecodeError::InvalidCmap;
    }
    previous = last;
  }
  return DecodeError::None;
}

DecodeError Decoder::map() {
  if (cmap.size < 4 || u16(cmap.data)) return DecodeError::InvalidCmap;
  const uint32_t count = u16(cmap.data + 2);
  if (!has(cmap, 4, count * 8)) return DecodeError::InvalidCmap;
  Bytes chosen{};
  uint32_t format = 0, rank = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto *p = cmap.data + 4 + i * 8;
    const uint32_t platform = u16(p), encoding = u16(p + 2), offset = u32(p + 4);
    if (offset < 4 + count * 8 || !has(cmap, offset, 2)) return DecodeError::InvalidCmap;
    if (!(platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))) continue;
    const uint32_t candidate = u16(cmap.data + offset);
    if (candidate != 4 && candidate != 12) continue;
    const uint32_t candidateRank = (candidate == 12 ? 4 : 0) + (platform == 3 ? 2 : 1);
    if (!has(cmap, offset, candidate == 12 ? 16 : 4)) return DecodeError::InvalidCmap;
    const uint32_t size = candidate == 12 ? u32(cmap.data + offset + 4) : u16(cmap.data + offset + 2);
    if (!has(cmap, offset, size)) return DecodeError::InvalidCmap;
    if (candidateRank > rank) {
      chosen = {cmap.data + offset, size}; rank = candidateRank; format = candidate;
    }
  }
  if (!chosen.data) return DecodeError::UnsupportedFormat;
  const auto error = format == 12 ? map12(chosen) : map4(chosen);
  if (error == DecodeError::None && sections[4].empty()) return DecodeError::InvalidCmap;
  return error;
}

DecodeError Decoder::visit(uint32_t index, uint32_t depth) {
  if (++graphWork > MaxGraphWork || depth > MaxComponentDepth) return DecodeError::ResourceLimit;
  auto &e = expansions[index];
  if (e.state == 1) return DecodeError::CompositeCycle;
  e.state = 1;
  const auto &g = glyphs[index];
  e.points = g.points; e.contours = g.contours; e.height = 0;
  for (uint32_t i = 0; i < g.components; ++i) {
    const auto &c = components[g.component + i];
    const auto error = visit(c.glyph, depth + 1);
    if (error != DecodeError::None) return error;
    const auto &child = expansions[c.glyph];
    if (!(c.flags & 2) && (uint32_t(c.args[0]) >= e.points || uint32_t(c.args[1]) >= child.points))
      return DecodeError::InvalidComponent;
    if (child.points > MaxPointsPerGlyph - e.points || child.contours > MaxContoursPerGlyph - e.contours)
      return DecodeError::ResourceLimit;
    e.points += child.points; e.contours += child.contours;
    if (e.height < child.height + 1) e.height = child.height + 1;
  }
  if (g.kind == 2 && (e.points > limits[2] || e.contours > limits[3] || e.height > limits[12]))
    return DecodeError::InvalidLimits;
  e.state = 2;
  return DecodeError::None;
}

DecodeError Decoder::run(Buffer &out) {
  auto error = directory();
  if (error != DecodeError::None) return error;
  if (head.size < 54 || hhea.size < 36 || maxp.size != 32 ||
      u32(head.data) != 0x10000 || u32(head.data + 12) != 0x5f0f3cf5 ||
      u32(hhea.data) != 0x10000 || u32(maxp.data) != 0x10000 ||
      s16(head.data + 52) != 0 || s16(hhea.data + 32) != 0)
    return DecodeError::InvalidMetrics;
  glyphCount = u16(maxp.data + 4);
  const uint32_t units = u16(head.data + 18), metrics = u16(hhea.data + 34);
  const int32_t locFormat = s16(head.data + 50);
  if (!glyphCount || glyphCount > MaxGlyphs) return DecodeError::ResourceLimit;
  if (units < 16 || units > 16384 || !metrics || metrics > glyphCount ||
      (locFormat != 0 && locFormat != 1) ||
      hmtx.size != metrics * 4 + (glyphCount - metrics) * 2 ||
      loca.size != (glyphCount + 1) * (locFormat ? 4 : 2))
    return DecodeError::InvalidMetrics;
  for (uint32_t i = 0; i < 13; ++i) limits[i] = u16(maxp.data + 6 + i * 2);
  if (limits[0] > MaxPointsPerGlyph || limits[1] > MaxContoursPerGlyph ||
      limits[2] > MaxPointsPerGlyph || limits[3] > MaxContoursPerGlyph ||
      limits[4] < 1 || limits[4] > 2 || limits[10] > MaxProgramBytes ||
      limits[11] > MaxComponentsPerGlyph || limits[12] > MaxComponentDepth)
    return DecodeError::InvalidLimits;
  if (cvt.size & 1) return DecodeError::InvalidLimits;
  if (cvt.size / 2 > 65536 || !room(uint32_t(cvt.size * 2))) return DecodeError::ResourceLimit;
  for (size_t i = 0; i < cvt.size; i += 2) word(sections[5], uint32_t(s16(cvt.data + i)));
  if (!copy(sections[7], fpgm) || !copy(sections[8], prep)) return DecodeError::ResourceLimit;
  glyphs.resize(glyphCount); expansions.resize(glyphCount);
  uint32_t previous = 0;
  for (uint32_t i = 0; i <= glyphCount; ++i) {
    const uint32_t offset = locFormat ? u32(loca.data + i * 4) : uint32_t(u16(loca.data + i * 2)) * 2;
    if (offset < previous || offset > glyf.size || (!i && offset != 0)) return DecodeError::InvalidLoca;
    if (i) {
      error = glyph(i - 1, {glyf.data + previous, offset - previous});
      if (error != DecodeError::None) return error;
    }
    previous = offset;
  }
  if (glyf.size - previous > 3) return DecodeError::InvalidLoca;
  for (size_t i = previous; i < glyf.size; ++i) if (glyf.data[i]) return DecodeError::InvalidLoca;
  for (uint32_t i = 0; i < glyphCount; ++i) {
    auto &g = glyphs[i];
    const uint32_t metric = i < metrics ? i : metrics - 1;
    g.advance = u16(hmtx.data + metric * 4);
    g.lsb = i < metrics ? s16(hmtx.data + i * 4 + 2) : s16(hmtx.data + metrics * 4 + (i - metrics) * 2);
    error = visit(i, 0);
    if (error != DecodeError::None) return error;
    if (!room(GlyphSize)) return DecodeError::ResourceLimit;
    auto &b = sections[0];
    word(b, g.kind); word(b, g.advance); word(b, uint32_t(g.lsb));
    for (const auto bound : g.bounds) word(b, uint32_t(bound));
    word(b, g.point); word(b, g.points); word(b, g.contour); word(b, g.contours);
    word(b, g.component); word(b, g.components); word(b, g.program); word(b, g.programs);
  }
  error = map();
  if (error != DecodeError::None) return error;
  if (!room(LimitsSize)) return DecodeError::ResourceLimit;
  for (const auto limit : limits) word(sections[9], limit);
  uint32_t weight = 400;
  if (os2.data) {
    if (os2.size < 8) return DecodeError::InvalidMetrics;
    weight = u16(os2.data + 4);
    if (weight < 1 || weight > 1000) return DecodeError::InvalidMetrics;
  }
  if (post.data && post.size < 16) return DecodeError::InvalidMetrics;
  uint32_t style = (u16(head.data + 44) & 2) ? 1 : 0;
  if (post.data && u32(post.data + 12)) style |= 2;
  uint32_t length = PayloadStart;
  for (const auto &section : sections) {
    if (section.size() > MaxResourceBytes - length) return DecodeError::ResourceLimit;
    length = aligned(length + uint32_t(section.size()));
    if (length > MaxResourceBytes) return DecodeError::ResourceLimit;
  }
  out.resize(length);
  uint32_t header[] = {Magic, Version, HeaderSize, length, SectionCount, glyphCount,
    units, uint32_t(s16(hhea.data + 4)), uint32_t(s16(hhea.data + 6)),
    uint32_t(s16(hhea.data + 8)), weight, style};
  for (uint32_t i = 0; i < 12; ++i) write_u32(out.data() + i * 4, header[i]);
  uint32_t offset = PayloadStart;
  for (uint32_t i = 0; i < SectionCount; ++i) {
    auto *entry = out.data() + HeaderSize + i * DirectoryEntrySize;
    write_u32(entry, i + 1); write_u32(entry + 4, offset);
    write_u32(entry + 8, uint32_t(sections[i].size()));
    for (size_t j = 0; j < sections[i].size(); ++j) out[offset + j] = sections[i][j];
    offset = aligned(offset + uint32_t(sections[i].size()));
  }
  return DecodeError::None;
}
} // namespace

DecodeError decode(Bytes file, kstd::vector<uint8_t> &resource) {
  resource.clear();
  Decoder decoder(file);
  const auto error = decoder.run(resource);
  if (error != DecodeError::None) resource.clear();
  return error;
}

const char *error_message(DecodeError error) {
  switch (error) {
    case DecodeError::None: return "success";
    case DecodeError::InvalidSize: return "invalid SFNT size (maximum 4 MiB)";
    case DecodeError::UnsupportedFormat: return "unsupported font format or Unicode cmap";
    case DecodeError::InvalidDirectory: return "invalid, duplicate or overlapping SFNT table";
    case DecodeError::MissingTable: return "required TrueType table missing";
    case DecodeError::InvalidMetrics: return "invalid font metrics or table version";
    case DecodeError::InvalidLimits: return "invalid maxp limits or CVT";
    case DecodeError::InvalidLoca: return "invalid glyph location table";
    case DecodeError::InvalidGlyph: return "truncated or malformed glyph";
    case DecodeError::InvalidFlags: return "invalid glyph flags or flag repeat";
    case DecodeError::InvalidCoordinates: return "glyph coordinate out of bounds";
    case DecodeError::InvalidCmap: return "malformed Unicode cmap or glyph index";
    case DecodeError::InvalidComponent: return "invalid composite or point attachment";
    case DecodeError::CompositeCycle: return "cyclic composite glyph";
    case DecodeError::ResourceLimit: return "font exceeds bounded resource limits";
  }
  return "unknown decoder error";
}
} // namespace undos::font::loader
