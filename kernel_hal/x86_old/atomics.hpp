
#pragma once

#include "ia32.hpp"

namespace hal::x86 {
enum class AtomicPlatorm : uint8_t {
  X86_386,      //< Spinlock basically
  X86_XADD,     //< 486, use XADD
  X86_CMPXCHG,  //< Compare and exchange
  X86_CMPXCHG8B //< Pentium+, CPUID CX8 feature bit, adds native 64-bit CMPXCHG8B
};

void init_atomic_ops(AtomicPlatorm platform);
}// namespace hal::x86
