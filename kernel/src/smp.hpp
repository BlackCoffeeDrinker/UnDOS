#pragma once

#include <stdint.h>

namespace kernel::smp {

// Ask the HAL to bring every CPU it reports beyond the bootstrap processor
// online. Each one that starts enters ap_entry() and joins the scheduler.
// A HAL that cannot start a CPU (every uniprocessor HAL) is not an error:
// the CPU is simply logged and left offline.
void start_secondary_cpus() noexcept;

// Entry point an application processor arrives at, handed to the HAL by
// start_secondary_cpus(). Never returns: it falls into that CPU's idle loop.
[[noreturn]] void ap_entry(uint32_t cpu_id) noexcept;

}// namespace kernel::smp
