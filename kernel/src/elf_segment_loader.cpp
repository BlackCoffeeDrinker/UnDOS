
#include "elf_segment_loader.hpp"

#include <Kernel.hpp>

bool kernel::elf::MapAndCopySegment(uintptr_t virt_start, const uint8_t *data, size_t file_size, size_t mem_size, kernel::vmm::ProtectFlags flags) noexcept {
  const uintptr_t start_page = virt_start & ~0xFFFUL;
  const uintptr_t end_page = (virt_start + mem_size + 4095) & ~0xFFFUL;

  for (uintptr_t page = start_page; page < end_page; page += 4096) {
    auto phys = HAL_PMM_AllocateFrames(1);
    if (!phys) return false;

    if (!HAL_VMM_MapPage(kernel::VirtualAddress(page), phys, flags)) {
      HAL_PMM_FreeFrames(phys, 1);
      return false;
    }
    HAL_VMM_Flush(kernel::VirtualAddress(page));

    __builtin_memset(reinterpret_cast<void *>(page), 0, 4096);
  }

  if (file_size > 0) {
    __builtin_memcpy(reinterpret_cast<void *>(virt_start), data, file_size);
  }
  return true;
}
