
#pragma once

#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel::driver {
void init(const kernel::BootInfoT &boot_info);
}// namespace kernel::driver
