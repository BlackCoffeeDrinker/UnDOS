
#pragma once

#include <kernel/__core.hpp>

UNDOS_KERNEL_PUBLIC_V1API(
    void,
    KE_USER_CreateProcess,
    const kstd::string_view &exec_path);
