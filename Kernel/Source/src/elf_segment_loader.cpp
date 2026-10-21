
#include "elf_segment_loader.hpp"

#include <Kernel.hpp>

kstd::error_code kernel::elf::MapAndCopySegment(uintptr_t virt_start, const uint8_t *data, size_t file_size, size_t mem_size, kernel::vmm::ProtectFlags flags) noexcept {
  if ((file_size != 0 && !data) || file_size > mem_size) return kernel::KernelError::InvalidArgument;
  if (mem_size > UINTPTR_MAX - virt_start || virt_start + mem_size > UINTPTR_MAX - 4095) {
    return kernel::KernelError::ArithmeticOverflow;
  }
  const uintptr_t start_page = virt_start & ~0xFFFUL;
  const uintptr_t end_page = (virt_start + mem_size + 4095) & ~0xFFFUL;

  for (uintptr_t page = start_page; page < end_page; page += 4096) {
    auto phys = HAL_PMM_AllocateFrames(1);
    if (!phys) {
      for (uintptr_t rollback = start_page; rollback < page; rollback += 4096) {
        const auto mapped_phys = HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(rollback));
        HAL_VMM_UnmapPage(kernel::VirtualAddress(rollback));
        if (mapped_phys) HAL_PMM_FreeFrames(mapped_phys, 1);
      }
      return kernel::KernelError::OutOfMemory;
    }

    if (!HAL_VMM_MapPage(kernel::VirtualAddress(page), phys, flags)) {
      HAL_PMM_FreeFrames(phys, 1);
      for (uintptr_t rollback = start_page; rollback < page; rollback += 4096) {
        const auto mapped_phys = HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(rollback));
        HAL_VMM_UnmapPage(kernel::VirtualAddress(rollback));
        if (mapped_phys) HAL_PMM_FreeFrames(mapped_phys, 1);
      }
      return kernel::KernelError::DeviceError;
    }
    HAL_SMP_ShootdownTlb(kernel::VirtualAddress(page));

    __builtin_memset(reinterpret_cast<void *>(page), 0, 4096);
  }

  if (file_size > 0) {
    __builtin_memcpy(reinterpret_cast<void *>(virt_start), data, file_size);
  }
  return {};
}
