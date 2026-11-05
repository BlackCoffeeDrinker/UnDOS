#include "crt_init.hpp"

#include <stdint.h>

// Kernel image C++ runtime start-up.
//
// Constructor policy (Docs/KernelGlobalConstructors.md): prefer constinit
// globals. A dynamic initialiser must not allocate, log, lock, call the HAL
// or depend on another global; it runs before HAL_PLATFORM_Init. Destructors
// are never run (the kernel does not exit), so registration is a no-op.

using InitFunction = void (*)();

extern "C" {
// Bracketing symbols from Kernel/Source/linker.ld (.init_array + .ctors).
extern InitFunction __init_array_start[];
extern InitFunction __init_array_end[];

void *__dso_handle = nullptr;

int __cxa_atexit(void (*)(void *), void *, void *) {
  return 0;
}
}

namespace {
constexpr uint32_t kMarkerValue = 0xC7095A11u;

volatile uint32_t g_ctorMarker = 0;
bool g_constructorsRan = false;

// Trivial dynamic initialiser: proves the walk reached this image's table.
struct CtorMarker {
  CtorMarker() noexcept { g_ctorMarker = kMarkerValue; }
};
CtorMarker g_marker;
}// namespace

namespace kernel::crt {

void run_global_constructors() noexcept {
  if (g_constructorsRan) return;
  g_constructorsRan = true;
  for (InitFunction *fn = __init_array_start; fn != __init_array_end; ++fn) {
    // Legacy .ctors lists may carry 0 / -1 sentinels.
    const auto value = reinterpret_cast<uintptr_t>(*fn);
    if (value == 0 || value == static_cast<uintptr_t>(-1)) continue;
    (*fn)();
  }
}

bool global_constructors_ok() noexcept {
  return g_ctorMarker == kMarkerValue;
}

}// namespace kernel::crt
