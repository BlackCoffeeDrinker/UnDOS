#pragma once

#include <kernel/scheduler.hpp>
#include <stdint.h>

namespace kernel::sched {

enum class ExecutionClass : uint8_t {
  Process,
  System,
  Idle,
};

struct ExecutionAccounting {
  uint64_t total_ticks{0};
  uint64_t idle_ticks{0};
  uint64_t system_ticks{0};
  uint64_t counter_epoch{1};
  bool clock_available{false};
};

inline ExecutionClass classify_execution(bool has_thread, bool has_process,
                                         bool is_idle) noexcept {
  if (!has_thread) return ExecutionClass::System;
  if (is_idle) return ExecutionClass::Idle;
  return has_process ? ExecutionClass::Process : ExecutionClass::System;
}

// Caller excludes local interrupts for both the CPU and process counters.
inline void account_timer_tick(
    ExecutionAccounting &state, ExecutionClass execution_class, InterruptedMode mode,
    uint64_t *process_ticks, uint64_t *user_ticks,
    uint64_t *kernel_ticks) noexcept {
  if (!state.clock_available) return;

  const bool process = execution_class == ExecutionClass::Process &&
      process_ticks && user_ticks && kernel_ticks;
  const bool idle = execution_class == ExecutionClass::Idle;
  const bool system = !process && !idle;
  uint64_t *split = process ? (mode == InterruptedMode::User ? user_ticks : kernel_ticks)
                           : nullptr;
  if (state.total_ticks == UINT64_MAX ||
      (idle && state.idle_ticks == UINT64_MAX) ||
      (system && state.system_ticks == UINT64_MAX) ||
      (process && (*process_ticks == UINT64_MAX || *split == UINT64_MAX))) {
    if (state.counter_epoch == UINT64_MAX) {
      state.clock_available = false;
      return;
    }
    ++state.counter_epoch;
    state.total_ticks = state.idle_ticks = state.system_ticks = 0;
    if (process) *process_ticks = *user_ticks = *kernel_ticks = 0;
  }

  ++state.total_ticks;
  if (idle) ++state.idle_ticks;
  else if (system) ++state.system_ticks;
  else {
    ++*process_ticks;
    ++*split;
  }
}

}  // namespace kernel::sched
