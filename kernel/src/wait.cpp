#include "wait.hpp"

#include "stdkrn.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KEventObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/time.hpp>

#include "scheduler/Scheduler.hpp"

namespace kernel::wait {
namespace {
TimedWaitList g_timed;

KDispatcherHeader *header_of(const KObjectPtr<KObject> &object) noexcept {
  if (const auto event = object.As<KEventObject>()) return &event->header;
  if (const auto process = object.As<KProcessObject>()) return &process->exitHeader;
  if (const auto io = object.As<KIOObject>()) return io->waitable;
  return nullptr;
}

void wake(KThreadObject *thread) noexcept { sched::ready_locked(thread); }
} // namespace

void tick(uint64_t now_ms) noexcept {
  g_timed.Expire(now_ms, wake);
}
} // namespace kernel::wait

using namespace kernel;

UNDOS_KERNEL_API_DEF WaitStatus KE_SCHED_WaitForHeader(KDispatcherHeader &header,
                                                      kstd::chrono::milliseconds timeout) noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return WaitStatus::Invalid;

  // Check-then-block with interrupts off so a signal cannot slip in between.
  HAL_CPU_DisableInterrupts();
  if (header.TryAcquire()) {
    HAL_CPU_EnableInterrupts();
    return WaitStatus::Signalled;
  }
  if (timeout.count() == 0) {
    HAL_CPU_EnableInterrupts();
    return WaitStatus::Timeout;
  }

  KWaitBlock wb;
  wb.thread = self.get();
  header.AddWaiter(wb);
  const bool timed = timeout.count() > 0;
  if (timed) {
    wb.deadline_ms = static_cast<uint64_t>(KE_TIME_GetSystemTime().count()) +
                     static_cast<uint64_t>(timeout.count());
    wait::g_timed.Add(wb);
  }

  while (!wb.satisfied && !wb.timed_out) {
    sched::block_current_locked();
  }

  if (timed) wait::g_timed.Remove(wb);
  if (!wb.satisfied) header.RemoveWaiter(wb);
  HAL_CPU_EnableInterrupts();
  return wb.satisfied ? WaitStatus::Signalled : WaitStatus::Timeout;
}

UNDOS_KERNEL_API_DEF WaitStatus KE_SCHED_WaitForObject(const KObjectPtr<KObject> &object,
                                                      kstd::chrono::milliseconds timeout) noexcept {
  auto *header = wait::header_of(object);
  if (!header) return WaitStatus::Invalid;
  // Hold a reference for the duration of the wait so a concurrent close
  // cannot free the header under us.
  const KObjectPtr<KObject> keep = object;
  return KE_SCHED_WaitForHeader(*header, timeout);
}

UNDOS_KERNEL_API_DEF void KE_EVENT_SignalFromIsr(KDispatcherHeader &header, bool wakeAll) noexcept {
  header.Signal(wakeAll, wait::wake);
}

UNDOS_KERNEL_API_DEF void KE_EVENT_Signal(KDispatcherHeader &header, bool wakeAll) noexcept {
  HAL_CPU_DisableInterrupts();
  header.Signal(wakeAll, wait::wake);
  HAL_CPU_EnableInterrupts();
}

UNDOS_KERNEL_API_DEF KObjectPtr<KEventObject> KE_EVENT_Create(bool manualReset, bool initialState) noexcept {
  auto event = CreateKObject<KEventObject>("event");
  if (!event) return nullptr;
  event->header.manualReset = manualReset;
  event->header.signalled = initialState;
  return event;
}
