#pragma once

#include "gfx/font/font_system.hpp"

#include <kernel/kobject/KObjectT.hpp>

namespace kernel::font {

struct FontObject final : KObjectT<FontObject, 1, TYPE_FONT> {
  UiFont snapshot;
  uint32_t fallback{0};
  uint32_t generation{0};
  bool bitmap{true};
};

} // namespace kernel::font

namespace kernel {
template<>
struct ObjectTypeOf<TYPE_FONT> {
  using type = font::FontObject;
};
} // namespace kernel
