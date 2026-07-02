
#pragma once

#include <kernel/KObject.hpp>
#include <kernel/__core.hpp>

#include <string_view.hpp>

namespace kernel {
struct KDriverObject : KObjectT<KDriverObject, 1, TYPE_DRIVER> {
  cfunc<void(const KObjectPtr<KDriverObject>&, const KEvent &)> eventHandler;
  uintptr_t load_base{0};
  size_t total_size{0};
  cfunc<void(KObjectPtr<KDriverObject> &)> entry_point;
};
}// namespace kernel

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KDriverObject> KE_DRIVER_Load(const kstd::string_view &path);
