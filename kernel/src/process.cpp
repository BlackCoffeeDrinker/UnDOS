
#include "process.hpp"

#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KProcessObject.hpp>

UNDOS_KERNEL_API_DEF [[noreturn]] void kernel::process::terminate_current(int32_t code) noexcept {
  const auto thread = KE_SCHED_GetCurrentThread();

  if (thread && thread->owner_process) {
    const auto process = KObjectPtr<KProcessObject>(static_cast<KProcessObject *>(thread->owner_process));

    process->exit_code = code;
    process->state = KProcessObject::State::Terminated;

    // Free the physical frames tracked by this process's VADs and its
    // translation root; must happen before we drop the thread and switch
    // away from its address space for the last time.
    kernel::vmm::destroy_user_address_space(process->address_space);

    // Detach so releasing our KObjectPtrs below doesn't loop back here.
    thread->owner_process = nullptr;

    KE_OB_RemoveObject(KE_OB_GetProcessDirectory(), KObjectPtr<KObject>(process.get()));
  }

  KE_OB_RemoveObject(KE_OB_GetThreadsDirectory(), KObjectPtr<KObject>(thread.get()));

  // Hands off to the scheduler; never returns to this thread.
  KE_SCHED_Terminate();
}
