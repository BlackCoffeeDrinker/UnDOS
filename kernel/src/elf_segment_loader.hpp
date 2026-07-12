
#pragma once

#include "stdkrn.hpp"

#include <kernel/elf.hpp>

namespace kernel::elf {
// Maps [virt_start, virt_start + mem_size) page-by-page into whichever
// address space is currently active (i.e. whatever HAL_VMM_MapPage targets),
// zero-fills each page, then copies file_size bytes of segment data on top.
// Shared by the kernel driver loader and the user-mode process loader.
[[nodiscard]] bool MapAndCopySegment(uintptr_t virt_start, const uint8_t *data, size_t file_size, size_t mem_size, kernel::vmm::ProtectFlags flags) noexcept;
}// namespace kernel::elf
