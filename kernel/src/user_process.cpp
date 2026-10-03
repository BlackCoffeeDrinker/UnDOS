
#include "elf_loader.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/tty/tty_device.hpp>

#include <Kernel.hpp>


namespace {
// User stack size for the process's single (main) thread.
constexpr size_t USER_STACK_SIZE = 0x4000;// 16 KB
}// namespace

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KProcessObject> KE_USER_CreateProcess(const kstd::string_view &exec_path) noexcept {
  // Step 1 — Create an isolated address space for the process.
  const auto process = kernel::CreateKObject<kernel::KProcessObject>(exec_path);
  if (!process) {
    KE_Print("KE_USER_CreateProcess: failed to allocate KProcessObject");
    return nullptr;
  }

  // Inherit inheritable handles (e.g. standard streams) from parent process if present
  if (const auto currentThread = KE_SCHED_GetCurrentThread()) {
    if (currentThread->owner_process) {
      process->InheritHandlesFrom(currentThread->owner_process->handles);
    }
  }

  // If no handles were inherited, attach default console streams if available
  if (process->handles.entries.empty()) {
    if (auto defaultSink = KE_TTY_GetDefaultConsoleSink()) {
      if (auto ioObj = KE_IO_FromObject(defaultSink)) {
        process->AttachStandardStreams(ioObj);
      } else {
        process->AttachStandardStreams(defaultSink);
      }
    }
  }

  if (!kernel::vmm::create_user_address_space(process->address_space)) {
    KE_Print("KE_USER_CreateProcess: failed to create address space for {}", exec_path);
    return nullptr;
  }

  // Look up the long-lived subsystem that owns this loader, so the process
  // can later be queried for which subsystem bootstrapped it.
  const auto subsystem = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(
      KE_OB_GetSubsystemDirectory(), "NativeElfLoader");

  if (!subsystem) {
    KE_Print("KE_USER_CreateProcess: failed to find NativeElfLoader subsystem for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }
  process->subsystem = subsystem;

  // Step 2 — Load the Loader into the process's address space via the VFS.
  kernel::elf::LoadPolicy policy;
  policy.user_mode = true;
  policy.target_as = &process->address_space;

  const auto result = kernel::elf::LoadElfFromVfs(process->subsystem->loader_path, policy);
  if (!result.ok) {
    KE_Print("KE_USER_CreateProcess: failed to load Loader for: {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  const auto entry = result.entry_point;

  // A dedicated user-mode stack region, mapped USER|READ|WRITE.
  const void *user_stack = kernel::vmm::allocate_memory(
      process->address_space, USER_STACK_SIZE,
      kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!user_stack) {
    KE_Print("KE_USER_CreateProcess: failed to allocate user stack for {}", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  auto user_stack_top = kernel::VirtualAddress::from_ptr(user_stack) + USER_STACK_SIZE;

  // Step 3 — Loader context: write the real executable path onto the new
  // process's stack so the (not-yet-running) loader can retrieve it once its
  // thread starts. The stack was mapped while the process's address space
  // was not the active one, so temporarily switch CR3 to it -- mirrors the
  // pattern used by kernel::process::load_elf_from_vfs.
  {
    const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
    const uintptr_t irq_state = HAL_CPU_SaveAndDisableInterrupts();
    HAL_VMM_SwitchAddressSpace(process->address_space.translation_root);

    const auto path_len = exec_path.size() + 1;// include NUL terminator
    user_stack_top = (user_stack_top - path_len).align_down(4);
    auto *exec_path_dest = user_stack_top.as_ptr<char>();
    __builtin_memcpy(exec_path_dest, exec_path.data(), exec_path.size());
    exec_path_dest[exec_path.size()] = '\0';

    // Push a pointer to the path string, so the loader can find it at the
    // top of its initial stack (e.g. `mov eax, [esp]`).
    const auto exec_path_addr = user_stack_top;
    user_stack_top = (user_stack_top - sizeof(void *)).align_down(4);
    *user_stack_top.as_ptr<kernel::VirtualAddress>() = exec_path_addr;

    HAL_VMM_SwitchAddressSpace(caller_root);
    HAL_CPU_RestoreInterrupts(irq_state);
  }

  // Step 4 — Create the process's ring-3 main thread.
  const auto thread = KE_SCHED_CreateUserThread(process->address_space, entry, user_stack_top);
  if (!thread) {
    kernel::vmm::destroy_user_address_space(process->address_space);
    return nullptr;
  }

  thread->owner_process = process.get();
  process->main_thread = thread;

  // Step 5 — Register the process under \Processes (the thread is already
  // registered under \Threads by KE_SCHED_CreateUserThread).
  KE_OB_InsertObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.get()));

  // Step 6 — Ring-3 code can now reach the kernel via int 0x80 (see
  // kernel_hal/x86_old/intr.cpp's syscall_handler / kernel/src/syscall.cpp's
  // dispatcher for SYS_EXIT/SYS_WRITE).
  return process;
}
