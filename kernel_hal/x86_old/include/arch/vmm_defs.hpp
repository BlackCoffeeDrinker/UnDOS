
#pragma once

#include <stddef.h>

// arch/x86/include/arch/vmm_defs.hpp
namespace kernel::arch {
inline constexpr size_t PAGE_SIZE = 4096;
inline constexpr size_t PAGE_SHIFT = 12; // 2^12 = 4096
}
