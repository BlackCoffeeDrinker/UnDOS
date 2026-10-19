
#include "process.hpp"

#include "vmm.hpp"
#include "user/user.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KProcessObject.hpp>

[[noreturn]] void kernel::process::terminate_current(int32_t code) noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();

  if (thread && thread->owner_process) {
    const auto process = KObjectPtr(thread->owner_process);

    process->exit_code = code;
    process->state = KProcessObject::State::Terminated;

    // Windows, classes and message queues die with their process.
    kernel::user::on_process_exit(*process);
    process->fontResources.clear();

    // Release WaitProcess/WaitForObject callers.
    KE_EVENT_Signal(process->exitHeader, true);

    // Free the physical frames tracked by this process's VADs and its
    // translation root; must happen before we drop the thread and switch
    // away from its address space for the last time.
    kernel::vmm::destroy_user_address_space(process->address_space);

    // Detach so releasing our KObjectPtrs below doesn't loop back here.
    thread->owner_process = nullptr;

    const auto processRemoval = KE_OB_RemoveObject(KE_OB_GetProcessDirectory(), KObjectPtr<KObject>(process.get()));
    if (processRemoval != KernelError::Success) {
      KE_Print("process::terminate_current: failed to remove process object: {}",
               make_error_code(processRemoval).message());
    }
  }

  if (thread) {
    const auto threadRemoval = KE_OB_RemoveObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(thread.get()));
    if (threadRemoval != KernelError::Success) {
      KE_Print("process::terminate_current: failed to remove thread object: {}",
               make_error_code(threadRemoval).message());
    }
  }

  // Hands off to the scheduler; never returns to this thread.
  KE_SCHED_Terminate();
}
