#pragma once

#include <undos/font/resource.hpp>
#include <vector.hpp>

namespace undos::font::loader {

enum class DecodeError {
  None, InvalidSize, UnsupportedFormat, InvalidDirectory, MissingTable,
  InvalidMetrics, InvalidLimits, InvalidLoca, InvalidGlyph, InvalidFlags,
  InvalidCoordinates, InvalidCmap, InvalidComponent, CompositeCycle,
  ResourceLimit
};

// Output is empty on failure; input and output must not alias.
DecodeError decode(Bytes file, kstd::vector<uint8_t> &resource);
const char *error_message(DecodeError error);

} // namespace undos::font::loader
