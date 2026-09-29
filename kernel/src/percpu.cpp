#include <kernel/hal_interface.hpp>
#include <kernel/percpu.hpp>

namespace kernel::cpu {

// The platform owns CPU identity: on x86 this is the local APIC id mapped to
// a dense logical index, on a uniprocessor HAL it is simply 0. Clamp to the
// PerCpu<T> bound so a HAL reporting an id the kernel was not built for
// cannot index out of bounds.
size_t current_id() noexcept {
  const uint32_t id = HAL_CPU_GetCurrentId();
  if (id >= MAX_CPUS) {
    return 0;
  }
  return id;
}

}// namespace kernel::cpu
