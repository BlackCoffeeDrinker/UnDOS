#include <undos/font/resource.hpp>

namespace undos::font {
namespace {
bool range(uint32_t first, uint32_t count, size_t available) {
  return first <= available && count <= available - first;
}

bool coordinate(int32_t value) { return value >= -32768 && value <= 32767; }

struct Graph {
  Bytes glyphs;
  Bytes components;
  uint32_t work{0};
  uint32_t path[MaxComponentDepth + 1]{};

  Error visit(uint32_t id, uint32_t depth, uint32_t &points, uint32_t &contours, uint32_t &levels) {
    if (++work > MaxGraphWork) return Error::ResourceLimit;
    if (depth > MaxComponentDepth) return Error::ResourceLimit;
    for (uint32_t i = 0; i < depth; ++i) if (path[i] == id) return Error::InvalidComponent;
    path[depth] = id;
    const auto *glyph = glyphs.data + id * GlyphSize;
    points = read_u32(glyph + 32);
    contours = read_u32(glyph + 40);
    const uint32_t first = read_u32(glyph + 44), count = read_u32(glyph + 48);
    for (uint32_t i = 0; i < count; ++i) {
      const auto *component = components.data + (first + i) * ComponentSize;
      uint32_t childPoints = 0, childContours = 0, childLevels = 0;
      const auto error = visit(read_u32(component), depth + 1, childPoints, childContours, childLevels);
      if (error != Error::None) return error;
      if (!(read_u32(component + 4) & 2u) &&
          (read_u32(component + 8) >= points || read_u32(component + 12) >= childPoints))
        return Error::InvalidComponent;
      if (childPoints > MaxPointsPerGlyph - points || childContours > MaxContoursPerGlyph - contours)
        return Error::ResourceLimit;
      points += childPoints;
      contours += childContours;
      if (childLevels + 1 > levels) levels = childLevels + 1;
    }
    return Error::None;
  }
};
}

Error validate(Bytes bytes) {
  if (!bytes.data || bytes.size < PayloadStart || bytes.size > MaxResourceBytes) return Error::InvalidSize;
  const auto *header = bytes.data;
  if (read_u32(header) != Magic || read_u32(header + 8) != HeaderSize ||
      read_u32(header + 12) != bytes.size || read_u32(header + 16) != SectionCount)
    return Error::InvalidHeader;
  if (read_u32(header + 4) != Version) return Error::UnsupportedVersion;
  const uint32_t glyphCount = read_u32(header + 20), em = read_u32(header + 24);
  if (!glyphCount || glyphCount > MaxGlyphs) return Error::ResourceLimit;
  if (em < 16 || em > 16384 || !coordinate(read_i32(header + 28)) ||
      !coordinate(read_i32(header + 32)) || !coordinate(read_i32(header + 36)) ||
      read_u32(header + 40) < 1 || read_u32(header + 40) > 1000 ||
      (read_u32(header + 44) & ~3u)) return Error::InvalidMetrics;
  for (uint32_t offset = 48; offset < HeaderSize; offset += 4)
    if (read_u32(header + offset)) return Error::InvalidHeader;

  constexpr uint32_t strides[SectionCount] = {GlyphSize, PointSize, 4, ComponentSize, CmapSize, 4, 1, 1, 1, LimitsSize};
  uint32_t next = PayloadStart;
  for (uint32_t i = 0; i < SectionCount; ++i) {
    const auto *entry = header + HeaderSize + i * DirectoryEntrySize;
    const uint32_t offset = read_u32(entry + 4), size = read_u32(entry + 8);
    if (read_u32(entry) != i + 1 || offset != next || offset > bytes.size ||
        size > bytes.size - offset || size % strides[i]) return Error::InvalidDirectory;
    const uint32_t end = offset + size;
    next = aligned(end);
    if (next > bytes.size) return Error::InvalidDirectory;
    for (uint32_t pad = end; pad < next; ++pad) if (header[pad]) return Error::InvalidDirectory;
  }
  if (next != bytes.size) return Error::InvalidDirectory;
  const ResourceView view(bytes);
  const auto glyphs = view.section(Section::Glyphs), points = view.section(Section::Points);
  const auto contours = view.section(Section::Contours), components = view.section(Section::Components);
  const auto programs = view.section(Section::GlyphPrograms), cmap = view.section(Section::Cmap);
  const auto limits = view.section(Section::Limits);
  if (glyphs.size / GlyphSize != glyphCount || limits.size != LimitsSize) return Error::InvalidDirectory;
  if (view.section(Section::FontProgram).size > MaxProgramBytes ||
      view.section(Section::PrepProgram).size > MaxProgramBytes) return Error::InvalidProgram;
  if (view.section(Section::Cvt).size / 4 > 65536) return Error::ResourceLimit;
  for (size_t i = 0; i < view.section(Section::Cvt).size; i += 4)
    if (!coordinate(read_i32(view.section(Section::Cvt).data + i))) return Error::InvalidMetrics;
  // maxZones is the only maxp requirement that describes a mode rather than a capacity.
  if (read_u32(limits.data + 16) < 1 || read_u32(limits.data + 16) > 2) return Error::InvalidLimits;
  for (uint32_t i = 0; i < 13; ++i) if (read_u32(limits.data + i * 4) > 65535) return Error::InvalidLimits;
  if (read_u32(limits.data) > MaxPointsPerGlyph || read_u32(limits.data + 4) > MaxContoursPerGlyph ||
      read_u32(limits.data + 8) > MaxPointsPerGlyph || read_u32(limits.data + 12) > MaxContoursPerGlyph ||
      read_u32(limits.data + 44) > MaxComponentsPerGlyph || read_u32(limits.data + 48) > MaxComponentDepth)
    return Error::ResourceLimit;

  uint32_t usedPoints = 0, usedContours = 0, usedComponents = 0, usedPrograms = 0;
  for (uint32_t id = 0; id < glyphCount; ++id) {
    const auto *g = glyphs.data + id * GlyphSize;
    const uint32_t kind = read_u32(g), firstPoint = read_u32(g + 28), pointCount = read_u32(g + 32);
    const uint32_t firstContour = read_u32(g + 36), contourCount = read_u32(g + 40);
    const uint32_t firstComponent = read_u32(g + 44), componentCount = read_u32(g + 48);
    const uint32_t program = read_u32(g + 52), programSize = read_u32(g + 56);
    if (kind > 2 || read_u32(g + 4) > 65535 || !coordinate(read_i32(g + 8))) return Error::InvalidGlyph;
    for (uint32_t offset = 12; offset < 28; offset += 4)
      if (!coordinate(read_i32(g + offset))) return Error::InvalidGlyph;
    if (read_i32(g + 12) > read_i32(g + 20) || read_i32(g + 16) > read_i32(g + 24))
      return Error::InvalidGlyph;
    if (firstPoint != usedPoints || firstContour != usedContours || firstComponent != usedComponents ||
        program != usedPrograms || !range(firstPoint, pointCount, points.size / PointSize) ||
        !range(firstContour, contourCount, contours.size / 4) ||
        !range(firstComponent, componentCount, components.size / ComponentSize) ||
        !range(program, programSize, programs.size)) return Error::InvalidGlyph;
    if (pointCount > MaxPointsPerGlyph || contourCount > MaxContoursPerGlyph ||
        componentCount > MaxComponentsPerGlyph || programSize > MaxProgramBytes) return Error::ResourceLimit;
    if ((!kind && (pointCount || contourCount || componentCount || programSize)) ||
        (kind == 1 && componentCount) || (kind == 2 && (pointCount || contourCount || !componentCount)))
      return Error::InvalidGlyph;
    if (kind == 1 && (pointCount > read_u32(limits.data) || contourCount > read_u32(limits.data + 4)))
      return Error::InvalidLimits;
    if (programSize > read_u32(limits.data + 40) || componentCount > read_u32(limits.data + 44))
      return Error::InvalidLimits;
    for (uint32_t i = 0; i < pointCount; ++i) {
      const auto *p = points.data + (firstPoint + i) * PointSize;
      if (!coordinate(read_i32(p)) || !coordinate(read_i32(p + 4)) || read_u32(p + 8) > 1)
        return Error::InvalidPoint;
    }
    uint32_t end = 0;
    for (uint32_t i = 0; i < contourCount; ++i) {
      const uint32_t current = read_u32(contours.data + (firstContour + i) * 4);
      if (current >= pointCount || (i && current <= end)) return Error::InvalidContour;
      end = current;
    }
    if ((pointCount != 0) != (contourCount != 0) || (pointCount && end != pointCount - 1))
      return Error::InvalidContour;
    for (uint32_t i = 0; i < componentCount; ++i) {
      const auto *c = components.data + (firstComponent + i) * ComponentSize;
      const uint32_t flags = read_u32(c + 4);
      // Preserve standard component flags; reject reserved/conflicting encodings.
      if (read_u32(c) >= glyphCount || (flags & ~0x1fefu) || ((flags & 0x1800u) == 0x1800u) ||
          ((flags & 0x20u) != 0) != (i + 1 < componentCount) ||
          ((flags & 0x100u) && i + 1 != componentCount)) return Error::InvalidComponent;
      const uint32_t transforms = ((flags & 8u) ? 1u : 0u) + ((flags & 0x40u) ? 1u : 0u) +
                                  ((flags & 0x80u) ? 1u : 0u);
      if (transforms > 1) return Error::InvalidComponent;
      if (!(flags & 2u)) {
        if (read_u32(c + 8) > 65535 || read_u32(c + 12) > 65535) return Error::InvalidComponent;
      } else if (!coordinate(read_i32(c + 8)) || !coordinate(read_i32(c + 12))) return Error::InvalidComponent;
      for (uint32_t j = 16; j < ComponentSize; j += 4)
        if (!coordinate(read_i32(c + j))) return Error::InvalidComponent;
      if (!(flags & 1u)) {
        const int32_t minimum = (flags & 2u) ? -128 : 0, maximum = (flags & 2u) ? 127 : 255;
        if (read_i32(c + 8) < minimum || read_i32(c + 8) > maximum ||
            read_i32(c + 12) < minimum || read_i32(c + 12) > maximum) return Error::InvalidComponent;
      }
      const int32_t xx = read_i32(c + 16), xy = read_i32(c + 20);
      const int32_t yx = read_i32(c + 24), yy = read_i32(c + 28);
      if (!(flags & 0xc8u) && (xx != 16384 || yy != 16384 || xy || yx))
        return Error::InvalidComponent;
      if ((flags & 0x48u) && (xy || yx)) return Error::InvalidComponent;
      if ((flags & 8u) && xx != yy) return Error::InvalidComponent;
      if (i + 1 == componentCount && programSize && !(flags & 0x100u)) return Error::InvalidComponent;
    }
    usedPoints += pointCount;
    usedContours += contourCount;
    usedComponents += componentCount;
    usedPrograms += programSize;
  }
  if (usedPoints != points.size / PointSize || usedContours != contours.size / 4 ||
      usedComponents != components.size / ComponentSize || usedPrograms != programs.size)
    return Error::InvalidDirectory;
  uint32_t last = 0;
  if (!cmap.size) return Error::InvalidCmap;
  for (size_t offset = 0; offset < cmap.size; offset += CmapSize) {
    const auto *mapping = cmap.data + offset;
    const uint32_t first = read_u32(mapping), end = read_u32(mapping + 4), glyph = read_u32(mapping + 8);
    if (first > end || end > 0x10ffff || (first <= 0xdfff && end >= 0xd800) ||
        (offset && first <= last) || glyph >= glyphCount || end - first >= glyphCount - glyph)
      return Error::InvalidCmap;
    last = end;
  }
  Graph graph{glyphs, components};
  for (uint32_t id = 0; id < glyphCount; ++id) {
    uint32_t expandedPoints = 0, expandedContours = 0, levels = 0;
    const auto error = graph.visit(id, 0, expandedPoints, expandedContours, levels);
    if (error != Error::None) return error;
    if (read_u32(glyphs.data + id * GlyphSize) == 2 &&
        (expandedPoints > read_u32(limits.data + 8) || expandedContours > read_u32(limits.data + 12) ||
         levels > read_u32(limits.data + 48)))
      return Error::InvalidLimits;
  }
  return Error::None;
}

const char *error_message(Error error) {
  switch (error) {
    case Error::None: return "valid normalized font (execution not validated)";
    case Error::InvalidSize: return "invalid font resource size";
    case Error::InvalidHeader: return "invalid font resource header";
    case Error::UnsupportedVersion: return "unsupported font resource version";
    case Error::InvalidDirectory: return "invalid font resource directory";
    case Error::InvalidMetrics: return "invalid font metrics";
    case Error::InvalidGlyph: return "invalid normalized glyph";
    case Error::InvalidPoint: return "invalid original glyph point";
    case Error::InvalidContour: return "invalid contour endpoints";
    case Error::InvalidComponent: return "invalid composite glyph";
    case Error::InvalidCmap: return "invalid Unicode mapping";
    case Error::InvalidProgram: return "invalid font program size";
    case Error::InvalidLimits: return "invalid maxp requirements";
    case Error::ResourceLimit: return "font resource limit exceeded";
  }
  return "unknown font resource error";
}
}
