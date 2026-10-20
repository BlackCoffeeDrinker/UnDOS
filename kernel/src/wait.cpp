#include "wait.hpp"

#include "stdkrn.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KEventObject.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/spinlock.hpp>
#include <kernel/time.hpp>

#include "scheduler/Scheduler.hpp"

namespace kernel::wait {
namespace {
TimedWaitList g_timed;

// Protects every dispatcher header's waiter list and g_timed. Taken with
// lock_irqsave because KE_EVENT_Signal and the timer tick reach the same
// structures from interrupt context.
SpinLock g_lock;

KDispatcherHeader *header_of(const KObjectPtr<KObject> &object) noexcept {
  if (const auto event = object.As<KEventObject>()) return &event->header;
  if (const auto process = object.As<KProcessObject>()) return &process->exitHeader;
  if (const auto io = object.As<KIOObject>()) return io->waitable;
  return nullptr;
}

void wake(KThreadObject *thread) noexcept { sched::ready_locked(thread); }
}// namespace

SpinLock &lock() noexcept { return g_lock; }

void tick(kstd::chrono::nanoseconds now) noexcept {
  const uintptr_t state = g_lock.lock_irqsave();
  g_timed.Expire(now, wake);
  g_lock.unlock_irqrestore(state);
}
}// namespace kernel::wait

using namespace kernel;

UNDOS_KERNEL_API_DEF kstd::error_code KE_SCHED_WaitForHeader(KDispatcherHeader &header,
                                                        kstd::chrono::milliseconds timeout) noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return KernelError::NoCurrentThread;

  // Check-then-block under the wait lock so a signal cannot slip in between.
  const uintptr_t irq_state = wait::lock().lock_irqsave();
  if (header.TryAcquire()) {
    wait::lock().unlock_irqrestore(irq_state);
    return KernelError::Success;
  }
  if (timeout.count() == 0) {
    wait::lock().unlock_irqrestore(irq_state);
    return KernelError::Timeout;
  }

  KWaitBlock wb;
  wb.thread = self.get();
  header.AddWaiter(wb);
  const bool timed = timeout.count() > 0;
  if (timed) {
    wb.deadline = kstd::chrono::duration_cast<decltype(wb.deadline)>(KE_TIME_GetSystemTime() + timeout);
    wait::g_timed.Add(wb);
  }

  while (!wb.satisfied && !wb.timed_out) {
    // The lock must not be held across the context switch: the CPU that
    // signals us needs it. Interrupts stay masked throughout, so nothing on
    // this CPU can observe the gap; a remote signaller sees the thread marked
    // Blocked by block_current_locked and re-queues it.
    wait::lock().unlock();
    sched::block_current_locked();
    wait::lock().lock();
  }

  if (timed) wait::g_timed.Remove(wb);
  if (!wb.satisfied) header.RemoveWaiter(wb);
  wait::lock().unlock_irqrestore(irq_state);
  return wb.satisfied ? KernelError::Success : KernelError::Timeout;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_SCHED_WaitForObject(const KObjectPtr<KObject> &object,
                                                        kstd::chrono::milliseconds timeout) noexcept {
  auto *header = wait::header_of(object);
  if (!header) return KernelError::NotWaitable;
  // Hold a reference for the duration of the wait so a concurrent close
  // cannot free the header under us.
  const KObjectPtr<KObject> keep = object;
  return KE_SCHED_WaitForHeader(*header, timeout);
}

UNDOS_KERNEL_API_DEF void KE_EVENT_Signal(KDispatcherHeader &header, bool wakeAll) noexcept {
  // Usable from interrupt context: the lock masks interrupts itself and
  // restores whatever state the caller had.
  const uintptr_t state = wait::lock().lock_irqsave();
  header.Signal(wakeAll, wait::wake);
  wait::lock().unlock_irqrestore(state);
}

UNDOS_KERNEL_API_DEF kstd::expected<KObjectPtr<KEventObject>, kstd::error_code>
KE_EVENT_Create(bool manualReset, bool initialState) noexcept {
  auto event = CreateKObject<KEventObject>("event");
  if (!event) return kstd::unexpected(event.error());
  event.value()->header.manualReset = manualReset;
  event.value()->header.signalled = initialState;
  return *event;
}
