
#pragma once

#include "stdkrn.hpp"

namespace kernel::process {
// Terminates the calling thread's owning process: marks the KProcessObject
// terminated, tears down its AddressSpace (VADs + physical frames via
// HAL_VMM_DestroyAddressSpace), removes both the process and its thread from
// the Object Manager, then hands off to the scheduler. Never returns.
[[noreturn]] void terminate_current(int32_t code) noexcept;
}// namespace kernel::process
