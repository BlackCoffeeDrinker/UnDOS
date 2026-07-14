
#include "elf_loader.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>


namespace {
// User stack size for the process's single (main) thread.
constexpr size_t USER_STACK_SIZE = 0x4000;// 16 KB
}// namespace

UNDOS_KERNEL_API_DEF void KE_USER_CreateProcess(const kstd::string_view &exec_path) noexcept {
  // Step 1 — Create an isolated address space for the process.
  const auto process = kernel::CreateKObject<kernel::KProcessObject>(exec_path);
  if (!process) {
    early_print_fmt("KE_USER_CreateProcess: failed to allocate KProcessObject\r\n");
    return;
  }
  early_print_fmt("KE_USER_CreateProcess: created process object for {}\r\n", process->name);

  if (!kernel::vmm::create_user_address_space(process->address_space)) {
    early_print_fmt("KE_USER_CreateProcess: failed to create address space for {}\r\n", exec_path);
    return;
  }
  early_print_fmt("KE_USER_CreateProcess: created address space for {}: translation_root={}, current_base={}, limit={}\r\n",
                  process->name,
                  process->address_space.translation_root.value,
                  process->address_space.current_base.as_ptr<>(),
                  process->address_space.limit.as_ptr<>());

  // Look up the long-lived subsystem that owns this loader, so the process
  // can later be queried for which subsystem bootstrapped it.
  const auto subsystem = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(
      KE_OB_GetSubsystemDirectory(), "NativeElfLoader");

  if (!subsystem) {
    early_print_fmt("KE_USER_CreateProcess: failed to find NativeElfLoader subsystem for {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }
  early_print_fmt("KE_USER_CreateProcess: found {} subsystem for {}\r\n", subsystem->name, exec_path);
  process->subsystem = subsystem;

  // Step 2 — Load the Loader into the process's address space via the VFS.
  kernel::elf::LoadPolicy policy;
  policy.user_mode = true;
  policy.target_as = &process->address_space;

  const auto result = kernel::elf::LoadElfFromVfs(process->subsystem->loader_path, policy);
  if (!result.ok) {
    early_print_fmt("KE_USER_CreateProcess: failed to load Loader for: {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }

  const auto entry = result.entry_point;

  early_print_fmt("KE_USER_CreateProcess: load_elf_from_vfs for {} @ {} (loader entry {})\r\n",
                  process->subsystem->loader_path,
                  entry.as_ptr<>(),
                  process->subsystem->loader_entry.as_ptr<>());

  // A dedicated user-mode stack region, mapped USER|READ|WRITE.
  const void *user_stack = kernel::vmm::allocate_user_memory(
      process->address_space, USER_STACK_SIZE,
      kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!user_stack) {
    early_print_fmt("KE_USER_CreateProcess: failed to allocate user stack for {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }

  auto user_stack_top = kernel::VirtualAddress::from_ptr(user_stack) + USER_STACK_SIZE;
  early_print_fmt("KE_USER_CreateProcess: allocated user stack for {} @ {}\r\n", exec_path, user_stack_top.as_ptr<>());

  // Step 3 — Loader context: write the real executable path onto the new
  // process's stack so the (not-yet-running) loader can retrieve it once its
  // thread starts. The stack was mapped while the process's address space
  // was not the active one, so temporarily switch CR3 to it -- mirrors the
  // pattern used by kernel::process::load_elf_from_vfs.
  {
    const kernel::PhysicalAddress caller_root = HAL_VMM_GetCurrentTranslationRoot();
    HAL_CPU_DisableInterrupts();
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
    HAL_CPU_EnableInterrupts();
  }

  // Step 4 — Create the process's ring-3 main thread.
  const auto thread = KE_SCHED_CreateUserThread(process->address_space, entry, user_stack_top);
  if (!thread) {
    early_print_fmt("KE_USER_CreateProcess: failed to create user thread for {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }
  early_print_fmt("KE_USER_CreateProcess: created user thread for {} @ {}\r\n", exec_path, thread->name);

  thread->owner_process = process.get();
  process->main_thread = thread;

  // Step 5 — Register the process under \Processes (the thread is already
  // registered under \Threads by KE_SCHED_CreateUserThread).
  KE_OB_InsertObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.get()));

  early_print_fmt("KE_USER_CreateProcess: launched {} (entry={}, stack={})\r\n",
                  exec_path, entry.as_ptr<>(), user_stack_top.as_ptr<>());

  // Step 6 — Ring-3 code can now reach the kernel via int 0x80 (see
  // kernel_hal/x86_old/intr.cpp's syscall_handler / kernel/src/syscall.cpp's
  // dispatcher for SYS_EXIT/SYS_WRITE).
}
