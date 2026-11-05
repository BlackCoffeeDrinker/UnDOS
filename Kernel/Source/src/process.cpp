
#include "process.hpp"
#include "process_notify.hpp"

#include "object_manager.hpp"
#include "scheduler/Scheduler.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KProcessObject.hpp>

[[noreturn]] void kernel::process::terminate_current(int32_t code) noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();

  if (thread && thread->owner_process) {
    const auto process = KObjectPtr(thread->owner_process);

    const uintptr_t processLock = objectmanager::lock_process_directory();
    process->exit_code = code;
    process->state = KProcessObject::State::Exiting;
    objectmanager::unlock_process_directory(processLock);

    // Subscribers (USER, then GFX) tear down their per-process state.
    ps_notify(ProcessEvent::Exiting, *process);

    // Release WaitProcess/WaitForObject callers.
    KE_EVENT_Signal(process->exitHeader, true);

    // Free the physical frames tracked by this process's VADs and its
    // translation root; must happen before we drop the thread and switch
    // away from its address space for the last time.
    kernel::vmm::destroy_user_address_space(process->address_space);

    // Exclude the timer-tick interrupt while detaching the owner used by tick accounting.
    const uintptr_t ownerLock = objectmanager::lock_process_directory();
    thread->owner_process = nullptr;
    objectmanager::unlock_process_directory(ownerLock);

    if (const auto processRemoval = KE_OB_RemoveObject(KE_OB_GetProcessDirectory(), process)) {
      KE_Print("process::terminate_current: failed to remove process object: {}",
               processRemoval.message());
    }
    process->state = KProcessObject::State::Terminated;
  }

  if (thread) {
    if (const auto threadRemoval = KE_OB_RemoveObject(KE_OB_GetThreadsDirectory(), thread)) {
      KE_Print("process::terminate_current: failed to remove thread object: {}",
               threadRemoval.message());
    }
  }

  // Hands off to the scheduler; never returns to this thread.
  KE_SCHED_Terminate();
}
