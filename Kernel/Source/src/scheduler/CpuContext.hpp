#pragma once

#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/percpu.hpp>
#include <kernel/spinlock.hpp>

#include "ReadyQueue.hpp"

namespace kernel::cpu {

/**
 * @ingroup SCHED
 * @brief All scheduling state owned by one logical CPU.
 *
 * Every field except `ready` is only ever written by the CPU that owns the
 * slot, so it needs no lock. `ready` is the exception: any CPU may make a
 * thread runnable on any other CPU, so it is protected by `ready_lock`, which
 * must always be taken with SpinLock::lock_irqsave() because the timer ISR
 * reaches the queue too.
 */
struct CpuContext {
  uint32_t id = 0;
  bool online = false;

  // Owner-CPU only.
  KObjectPtr<KThreadObject> current;
  KObjectPtr<KThreadObject> idle;
  uint64_t ticks = 0;

  // Guarded by ready_lock.
  sched::ReadyQueue ready;
  SpinLock ready_lock;
};

/// Every slot, including CPUs that were never brought online.
PerCpu<CpuContext> &contexts() noexcept;

/// The calling CPU's slot (== contexts()[current_id()]).
CpuContext &current_context() noexcept;

/// Number of slots whose `online` flag is set.
uint32_t online_count() noexcept;

/// Stamp each slot with its own id. Called once, very early on the BSP.
void init_contexts() noexcept;

}// namespace kernel::cpu
