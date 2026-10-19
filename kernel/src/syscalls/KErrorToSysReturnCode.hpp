#pragma once

#include <undos/syscalls/__enum_SyscallReturnCode.h>

#include <kernel/kerror.hpp>

namespace kernel {
SyscallReturnCode KErrorToSysReturnCode(KernelError error);
SyscallReturnCode KErrorToSysReturnCode(const kstd::error_code &error);
}
