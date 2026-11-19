#pragma once

#include <stdint.h>

namespace kernel {
struct KThreadObject;
}

namespace kernel::sched {

/**
 * @ingroup SCHED
 * @brief Read-only per-CPU sampling view for the profiler hooks.
 *
 * Both calls read the calling CPU's scheduler context and are compiled without
 * -finstrument-functions (CpuContext.cpp), so instrumentation hooks may call
 * them. Interrupts must be off for a consistent result.
 */

/// Timer ticks seen by the calling CPU.
uint64_t sample_cpu_ticks() noexcept;

/// Thread running on the calling CPU, or nullptr before the scheduler starts.
const KThreadObject *sample_current_thread() noexcept;

}// namespace kernel::sched
