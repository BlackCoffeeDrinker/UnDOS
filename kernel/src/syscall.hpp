
#pragma once

#include "stdkrn.hpp"

namespace kernel::syscall {
// Platform-agnostic syscall dispatcher. Called by KE_SYSCALL_Dispatch (the
// top-level, HAL-callable entry point defined in syscall.cpp) with the raw
// EAX/EBX/ECX/EDX values read off the trap frame by the HAL's int 0x80 gate.
int32_t dispatch(uint32_t number, uint32_t arg0, uint32_t arg1, uint32_t arg2) noexcept;
}// namespace kernel::syscall
