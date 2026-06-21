
#pragma once

#include <kernel/__core.hpp>
#include <kernel/boot/boot_info.hpp>

namespace kernel {
UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const boot_info_t &boot_info);
}// namespace kernel
