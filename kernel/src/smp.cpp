#include "smp.hpp"

#include <Kernel.hpp>

#include "scheduler/CpuContext.hpp"
#include "scheduler/Scheduler.hpp"
#include "stdkrn.hpp"

namespace kernel::smp {
namespace {

// How long to wait for a started AP to publish `online` before giving up on
// it. Purely a spin count: there is no wall clock this early on the AP.
constexpr uint32_t AP_START_SPIN_LIMIT = 100'000'000u;

kstd::error_code wait_until_online(const cpu::CpuContext &ctx) noexcept {
  for (uint32_t spins = 0; spins < AP_START_SPIN_LIMIT; ++spins) {
    HAL_ATOMIC_ThreadFence(PlatformMemoryOrder::Acquire);
    if (ctx.online) return {};
  }
  return KernelError::Timeout;
}

}// namespace

[[noreturn]] void ap_entry(const uint32_t cpu_id) noexcept {
  // Runs on the AP itself, on the stack start_secondary_cpus handed the HAL.
  // Interrupts are still masked here: init_ap requires that.
  sched::init_ap(cpu_id);

  // Publish ourselves only once the scheduling state is complete, so the BSP
  // never places a thread on a queue this CPU cannot yet drain.
  HAL_ATOMIC_ThreadFence(PlatformMemoryOrder::Release);

  HAL_CPU_EnableInterrupts();

  for (;;) {
    HAL_CPU_Halt();
    KE_SCHED_Yield();
  }
}

void start_secondary_cpus() noexcept {
  const uint32_t count = HAL_PLATFORM_GetCpuCount();
  if (count <= 1) {
    return;
  }

  const size_t slots = cpu::contexts().size();
  const size_t limit = count < slots ? count : slots;

  for (uint32_t id = 1; id < limit; ++id) {
    const auto base = sched::alloc_kernel_stack(sched::KERNEL_STACK_SIZE);
    if (!base) {
      KE_Print("SMP: no kernel stack for CPU {}, left offline", id);
      continue;
    }

    if (!HAL_SMP_StartProcessor(id,
                                cfunc<void(uint32_t)>(&ap_entry),
                                *base + sched::KERNEL_STACK_SIZE)) {
      KE_Print("SMP: HAL cannot start CPU {}, left offline", id);
      continue;
    }

    if (wait_until_online(cpu::contexts()[id])) {
      KE_Print("SMP: CPU {} never came online, left offline", id);
      continue;
    }

    KE_Print("SMP: CPU {} online", id);
  }
}

}// namespace kernel::smp
