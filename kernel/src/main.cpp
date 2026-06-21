

#include <Kernel.hpp>
#include <kernel/k_arch.hpp>

#include "pmm.hpp"

extern "C" void kernel_main() {
  ;
}

namespace kernel {
UNDOS_KERNEL_API void kernel_core_main(const boot_info_t &boot_info) {
  arch::early_print("Parsing system memory layout...\n\r");

  // Do full memory management initialization: Physical Memory Manager, Virtual Memory Manager, Heap Allocator
  pmm::init(boot_info);

  while (1) {};
}

}// namespace kernel
