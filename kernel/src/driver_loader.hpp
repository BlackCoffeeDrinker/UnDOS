
#pragma once

#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel::driver {
void init(const kernel::BootInfoT &boot_info);
KObjectPtr<KDriverObject> load_from_memory(PhysicalAddress module_base, size_t module_size);
}// namespace kernel::driver
