// Scheduler-side implementation of the wait/wake interface used by the
// interrupt dispatcher (sched_wait.hpp) and the kernel arena lock.

#include "sched_wait.hpp"

#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/scheduler.hpp>

namespace kernel::sched::wait {
namespace {
KObjectPtr<KThreadObject> g_worker;
}// namespace

kstd::error_code block_current(KDispatcherHeader &header) noexcept {
  return KE_SCHED_WaitForHeader(header, WAIT_INFINITE);
}

void wake(KDispatcherHeader &header) noexcept {
  KE_EVENT_Signal(header, false);
}

void yield_current() noexcept {
  KE_SCHED_Yield();
}

KThreadObject *current_thread() noexcept {
  return KE_SCHED_GetCurrentThread().get();
}

kstd::error_code ready_worker(void (*entry)(void *)) noexcept {
  auto thread = KE_SCHED_CreateThread(cfunc<void(void *)>(entry), nullptr, ThreadPriority::Realtime);
  if (!thread) return thread.error();
  g_worker = *thread;
  return {};
}

}// namespace kernel::sched::wait
