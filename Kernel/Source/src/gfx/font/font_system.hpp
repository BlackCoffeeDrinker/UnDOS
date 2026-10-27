#pragma once

#include "default_font.hpp"

namespace kernel::font {

// The kernel's font state: committed faces and the default font. The kernel
// has no atexit, so it is heap-allocated on first use and
// never destroyed, which lets it outlive every process table holding handles.
struct FontSystem {
  FaceRegistry faces;
  DefaultFont defaults{faces};
};

// Creates the system on first call. May allocate and block: never call it
// while holding the scene lock. Returns nullptr when allocation fails.
FontSystem *font_system();

// Returns the system if a font syscall already created it, else nullptr. It
// never allocates, so it is safe under the scene lock.
FontSystem *font_system_if_ready();

} // namespace kernel::font
