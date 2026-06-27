
#pragma once

#include <kernel/__core.hpp>
#include <kernel/boot/boot_info.hpp>

UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info);
