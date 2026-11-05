#pragma once

#include <stddef.h>
#include <stdint.h>

// Optional -finstrument-functions kernel profiler (CMake option UNDOS_PROFILE).
// With the option OFF this header provides only empty inline stubs and
// profiler.cpp is neither compiled nor linked.

namespace kernel::prof {

// Byte sink used by the dump; must be polled and safe with interrupts off.
using WriteFn = void (*)(const char *text, size_t size) noexcept;

#if UNDOS_PROFILE

struct ProfFrame {
  uint32_t fn;
  uint64_t start;
  uint64_t child;
};

struct ProfShadowStack {
  uint16_t depth;
  uint16_t overflow;
  ProfFrame f[64];
};

// Selects the timer (TSC or PIT channel 0) and enables recording.
void prof_init() noexcept;
// Writes the framed table through `write` (HAL_Debug_FirmwarePrint if null).
void prof_dump(WriteFn write = nullptr) noexcept;
void prof_pause() noexcept;
void prof_resume() noexcept;
// Starts a kernel thread that dumps (and resets) every `seconds`.
void prof_schedule_dump(uint32_t seconds) noexcept;

#else

// always_inline so that even -O0 builds emit no out-of-line stub symbols.
[[gnu::always_inline]] static inline void prof_init() noexcept {}
[[gnu::always_inline]] static inline void prof_dump(WriteFn = nullptr) noexcept {}
[[gnu::always_inline]] static inline void prof_pause() noexcept {}
[[gnu::always_inline]] static inline void prof_resume() noexcept {}
[[gnu::always_inline]] static inline void prof_schedule_dump(uint32_t) noexcept {}

#endif

}// namespace kernel::prof
