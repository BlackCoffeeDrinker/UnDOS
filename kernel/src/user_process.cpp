
#include "process_loader.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KProcessObject.hpp>

namespace {
// User stack size for the process's single (main) thread.
constexpr size_t USER_STACK_SIZE = 0x4000;// 16 KB
}// namespace

UNDOS_KERNEL_API_DEF void KE_USER_CreateProcess(const kstd::string_view &exec_path) {
  // Step 1 — Create an isolated address space for the process.
  auto process = kernel::CreateKObject<kernel::KProcessObject>(exec_path);
  if (!process) {
    early_print_fmt("KE_USER_CreateProcess: failed to allocate KProcessObject\r\n");
    return;
  }

  if (!kernel::vmm::create_user_address_space(process->address_space)) {
    early_print_fmt("KE_USER_CreateProcess: failed to create address space for {}\r\n", exec_path);
    return;
  }

  // Step 2 — Load the ELF into the process's address space via the VFS.
  const kernel::VirtualAddress entry = kernel::process::load_elf_from_vfs(process->address_space, exec_path);
  if (!entry) {
    early_print_fmt("KE_USER_CreateProcess: failed to load ELF: {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }

  // A dedicated user-mode stack region, mapped USER|READ|WRITE.
  void *user_stack = kernel::vmm::allocate_user_memory(
      process->address_space, USER_STACK_SIZE,
      kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
  if (!user_stack) {
    early_print_fmt("KE_USER_CreateProcess: failed to allocate user stack for {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }

  const kernel::VirtualAddress user_stack_top =
      kernel::VirtualAddress::from_ptr(user_stack) + USER_STACK_SIZE;

  // Step 3 — Create the process's ring-3 main thread.
  auto thread = KE_SCHED_CreateUserThread(process->address_space, entry, user_stack_top);
  if (!thread) {
    early_print_fmt("KE_USER_CreateProcess: failed to create user thread for {}\r\n", exec_path);
    kernel::vmm::destroy_user_address_space(process->address_space);
    return;
  }

  thread->owner_process = process.get();
  process->main_thread = thread;

  // Step 4 — Register the process under \Processes (the thread is already
  // registered under \Threads by KE_SCHED_CreateUserThread).
  KE_OB_InsertObject(KE_OB_GetProcessDirectory(), kernel::KObjectPtr<kernel::KObject>(process.get()));

  early_print_fmt("KE_USER_CreateProcess: launched {} (entry={}, stack={})\r\n",
                   exec_path, entry.as_ptr<>(), user_stack_top.as_ptr<>());

  // Step 5 — Ring-3 code can now reach the kernel via int 0x80 (see
  // kernel_hal/x86_old/intr.cpp's syscall_handler / kernel/src/syscall.cpp's
  // dispatcher for SYS_EXIT/SYS_WRITE).
}
