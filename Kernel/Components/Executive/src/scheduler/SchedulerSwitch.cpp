#include "Scheduler.hpp"

#include "stdkrn.hpp"
#include "CpuContext.hpp"
#include "ScheduleNext.hpp"
#include <percpu.hpp>

namespace kernel::sched {
void charge_timer_tick(InterruptedMode mode) noexcept {
  const uintptr_t interrupt_state = HAL_CPU_SaveAndDisableInterrupts();
  auto &context = cpu::current_context();
  KThreadObject *thread = context.current.get();
  KProcessObject *process = thread ? thread->owner_process : nullptr;
  const auto execution_class = sched::classify_execution(
      thread != nullptr, process != nullptr,
      thread && thread == context.idle.get());
  sched::account_timer_tick(
      context.accounting, execution_class, mode,
      process ? &process->cpuTicks : nullptr,
      process ? &process->userTicks : nullptr,
      process ? &process->kernelTicks : nullptr);
  HAL_CPU_RestoreInterrupts(interrupt_state);
}

void schedule_next() noexcept {
  auto &context = cpu::current_context();
  detail::schedule_next(
      context,
      [] { return HAL_VMM_GetCurrentTranslationRoot(); },
      [](PhysicalAddress root) { HAL_VMM_SwitchAddressSpace(root); },
      [] { return HAL_CPU_GetKernelStack(); },
      [](VirtualAddress stack) { HAL_CPU_SetKernelStack(stack); },
      [](VirtualAddress &previous, VirtualAddress next) {
        HAL_CPU_SwitchContext(previous, next);
      });
}
}  // namespace kernel::sched
