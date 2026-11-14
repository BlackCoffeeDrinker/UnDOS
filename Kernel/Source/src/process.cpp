
#include "process.hpp"
#include "process_notify.hpp"

#include "object_manager.hpp"
#include "scheduler/Scheduler.hpp"
#include "vmm.hpp"
#include "wait.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KProcessObject.hpp>

namespace {
using kernel::KProcessObject;
using kernel::KThreadObject;

// Last thread out: the process has no threads left that could touch its
// address space, so it can be torn down from here.
void teardown_process(const kernel::KObjectPtr<KProcessObject> &process) noexcept {
  // Subscribers (USER, then GFX) tear down their per-process state.
  ps_notify(kernel::ProcessEvent::Exiting, *process);

  // Release WaitProcess/WaitForObject callers.
  KE_EVENT_Signal(process->exitHeader, true);

  // Free the physical frames tracked by this process's VADs and its
  // translation root; must happen before we drop the thread and switch
  // away from its address space for the last time.
  kernel::vmm::destroy_user_address_space(process->address_space);

  if (const auto processRemoval = KE_OB_RemoveObject(KE_OB_GetProcessDirectory(), process)) {
    KE_Print("process::teardown: failed to remove process object: {}",
             processRemoval.message());
  }
  process->state = KProcessObject::State::Terminated;
}

// Wakes every sibling blocked in an alertable wait; the wait sees the
// process Exiting and returns, and the thread retires on its way out.
void wake_siblings(KProcessObject &process, const KThreadObject *self) noexcept {
  KThreadObject *siblings[KProcessObject::MaxThreads]{};
  const uintptr_t lock = kernel::objectmanager::lock_process_directory();
  for (uint32_t i = 0; i < KProcessObject::MaxThreads; ++i) {
    if (process.threads[i] != self) siblings[i] = process.threads[i];
  }
  kernel::objectmanager::unlock_process_directory(lock);

  const uintptr_t state = kernel::wait::lock().lock_irqsave();
  for (auto *sibling: siblings) {
    if (sibling && sibling->alertable) kernel::sched::ready_locked(sibling);
  }
  kernel::wait::lock().unlock_irqrestore(state);
}

[[noreturn]] void retire_current(uint32_t code) noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (thread) {
    thread->retiring = true;
    thread->alertable = false;
  }
  // Retirement may start from the timer interrupt; teardown needs to block.
  HAL_CPU_EnableInterrupts();

  if (thread && thread->owner_process) {
    const auto process = kernel::KObjectPtr(thread->owner_process);

    bool last = false;
    const uintptr_t lock = kernel::objectmanager::lock_process_directory();
    for (auto &slot: process->threads) {
      if (slot == thread.get()) slot = nullptr;
    }
    if (process->live_threads) --process->live_threads;
    last = process->live_threads == 0;
    if (last && process->state == KProcessObject::State::Running) {
      process->exit_code = static_cast<int32_t>(code);
      process->state = KProcessObject::State::Exiting;
    }
    kernel::objectmanager::unlock_process_directory(lock);

    thread->exit_code = code;
    KE_EVENT_Signal(thread->exitHeader, true);

    if (last) teardown_process(process);

    // Exclude the timer-tick interrupt while detaching the owner used by tick accounting.
    const uintptr_t ownerLock = kernel::objectmanager::lock_process_directory();
    thread->owner_process = nullptr;
    kernel::objectmanager::unlock_process_directory(ownerLock);
  }

  if (thread) {
    if (const auto threadRemoval = KE_OB_RemoveObject(KE_OB_GetThreadsDirectory(), thread)) {
      KE_Print("process::retire: failed to remove thread object: {}",
               threadRemoval.message());
    }
  }

  // Hands off to the scheduler; never returns to this thread.
  KE_SCHED_Terminate();
}
}// namespace

kstd::error_code kernel::process::attach_thread(KProcessObject &process, KThreadObject &thread) noexcept {
  const uintptr_t lock = objectmanager::lock_process_directory();
  kstd::error_code status = KernelError::NoCurrentProcess;
  if (process.state == KProcessObject::State::Running) {
    status = KernelError::OutOfMemory;
    for (auto &slot: process.threads) {
      if (!slot) {
        slot = &thread;
        ++process.live_threads;
        status = KernelError::Success;
        break;
      }
    }
  }
  objectmanager::unlock_process_directory(lock);
  return status;
}

[[noreturn]] void kernel::process::terminate_current(int32_t code) noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();

  if (thread && thread->owner_process) {
    auto *process = thread->owner_process;
    const uintptr_t processLock = objectmanager::lock_process_directory();
    // The first exit wins; a racing ExitProcess keeps the first code.
    if (process->state == KProcessObject::State::Running) {
      process->exit_code = code;
      process->state = KProcessObject::State::Exiting;
    }
    objectmanager::unlock_process_directory(processLock);
    wake_siblings(*process, thread.get());
  }
  retire_current(static_cast<uint32_t>(code));
}

[[noreturn]] void kernel::process::exit_current_thread(uint32_t code) noexcept {
  retire_current(code);
}

UNDOS_KERNEL_API_DEF void KE_SCHED_RetireIfExiting() noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();
  if (!thread || thread->retiring || !thread->owner_process) return;
  if (thread->owner_process->state == KProcessObject::State::Running) return;
  retire_current(static_cast<uint32_t>(thread->owner_process->exit_code));
}

UNDOS_KERNEL_API_DEF bool KE_SCHED_IsExitPending() noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();
  return thread && !thread->retiring && thread->owner_process &&
         thread->owner_process->state != KProcessObject::State::Running;
}
