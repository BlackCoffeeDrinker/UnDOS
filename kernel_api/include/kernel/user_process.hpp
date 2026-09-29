
#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>

namespace kernel {
struct KProcessObject;
}

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KProcessObject>,
    KE_USER_CreateProcess,
    const kstd::string_view &exec_path);
