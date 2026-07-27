#include <kernel/percpu.hpp>

namespace kernel::cpu {

// No HAL call exists yet to ask the platform which logical CPU is running
// the current code, so this always returns 0 (the only CPU that exists
// today). This is the single place that will be repointed at a real
// HAL_CPU_GetCurrentId() once an APIC-aware HAL exists; every PerCpu<T>
// call site is written against current_id() and will not need to change.
size_t current_id() noexcept {
  return 0;
}

}// namespace kernel::cpu
