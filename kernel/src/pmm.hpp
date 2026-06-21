
#pragma once

#include "Core.hpp"

/* This file contains the Physical Memory Manager (PMM) for the kernel.
 * The PMM is responsible for managing physical memory, allocating and deallocating frames.
 */
namespace kernel::pmm {
// Initialize the bitmap using the system memory map
void init(const boot_info_t &boot_info);


}// namespace kernel::pmm
