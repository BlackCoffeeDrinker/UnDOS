#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>


UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::boot_info_t &boot_info) {
  HAL_PlatformInit(boot_info);
  early_print("After PlatformInit\n\r");

  while (1) {};
}
