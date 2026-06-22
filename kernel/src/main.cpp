#include <Kernel.hpp>
#include <kernel/k_arch.hpp>

#include "pmm.hpp"


UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::boot_info_t &boot_info) {
  HAL_PlatformInit(boot_info);
  // Do full memory management initialization: Physical Memory Manager, Virtual Memory Manager, Heap Allocator
  kernel::pmm::init(boot_info);

  while (1) {};
}
