// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#include "stdkrn.hpp"

#include <memory.hpp>
#include <stddef.h>

#include "syscalls.hpp"

#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>

#include <undos/syscalls/__flags_FileOpenMode.h>


#include <undos/syscalls/__syscall_Print_V1.h>
#include <undos/syscalls/__syscall_CreateProcess_V1.h>
#include <undos/syscalls/__syscall_GetInfo_V1.h>
#include <undos/syscalls/__syscall_Exit_V1.h>
#include <undos/syscalls/__syscall_AllocatePages_V1.h>
#include <undos/syscalls/__syscall_FreePages_V1.h>
#include <undos/syscalls/__syscall_Protect_V1.h>
#include <undos/syscalls/__syscall_Open_V1.h>
#include <undos/syscalls/__syscall_Close_V1.h>
#include <undos/syscalls/__syscall_Read_V1.h>
#include <undos/syscalls/__syscall_Write_V1.h>

constexpr size_t UNDOS_MAX_COPY_SIZE = 4096;
constexpr size_t UNDOS_MAX_MAPPED_BUFFER_SIZE = 64 * 1024 * 1024;

static void KE_VMM_CopyToUserspace(kernel::vmm::AddressSpace &as, void *userPtr, const void *kernelPtr, size_t length) {
  if (userPtr == nullptr || length == 0) [[unlikely]] return;
  void *mapped = KE_VMM_MapBorrowed(userPtr, length, as);
  if (mapped == nullptr) [[unlikely]] return;
  kstd::memcpy(mapped, kernelPtr, length);
  KE_VMM_UnmapBorrowed(mapped, length, as);
}

static uint32_t Dispatch_Print_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_DEBUG_PRINT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_DEBUG_PRINT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_DEBUG_PRINT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_DEBUG_PRINT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_DEBUG_PRINT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_DEBUG_PRINT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.message != nullptr) {
    if (headerCopy.messageLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.messageLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_DEBUG_PRINT_V1 args {};
  if (headerCopy.message != nullptr && headerCopy.messageLength > 0) {
    args.message.resize(headerCopy.messageLength);
    if (args.message.data() == nullptr || args.message.size() != headerCopy.messageLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.message), headerCopy.messageLength, address_space)) {
      kstd::memcpy(args.message.data(), mapped, headerCopy.messageLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.messageLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Print_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateProcess_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEPROCESS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEPROCESS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_CREATEPROCESS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.pathLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  if (headerCopy.argument != nullptr) {
    if (headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.argumentLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_PROC_CREATEPROCESS_V1 args {};
  if (headerCopy.path != nullptr && headerCopy.pathLength > 0) {
    args.path.resize(headerCopy.pathLength);
    if (args.path.data() == nullptr || args.path.size() != headerCopy.pathLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space)) {
      kstd::memcpy(args.path.data(), mapped, headerCopy.pathLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.pathLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  if (headerCopy.argument != nullptr && headerCopy.argumentLength > 0) {
    args.argument.resize(headerCopy.argumentLength);
    if (args.argument.data() == nullptr || args.argument.size() != headerCopy.argumentLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.argument), headerCopy.argumentLength, address_space)) {
      kstd::memcpy(args.argument.data(), mapped, headerCopy.argumentLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.argumentLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  args.flags = headerCopy.flags;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateProcess_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetInfo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_GETINFO_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_GETINFO_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_GETINFO_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_GETINFO_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_GETINFO_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_GETINFO_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.commandLine != nullptr) {
    if (headerCopy.commandLineLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.commandLineLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_PROC_GETINFO_V1 args {};
  args.pid = headerCopy.pid;
  if (headerCopy.commandLine != nullptr) {
    args.commandLine = kernel::borrowed_ptr<char>(static_cast<char *>(KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.commandLine), headerCopy.commandLineLength, address_space)), headerCopy.commandLineLength, address_space);
    args.commandLine_len = headerCopy.commandLineLength;
  }

  (void)flags;
  const SyscallReturnCode ret = handle_GetInfo_V1(kstd::move(args));

  // content was written directly into the mapped user buffer; report the actual/required length.
  {
    const auto len = static_cast<uint32_t>(args.commandLine_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, commandLineLength), &len, sizeof(len));
  }
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, exitCode), &args.exitCode, sizeof(args.exitCode));
  return ret;
}

static uint32_t Dispatch_Exit_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_EXIT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_EXIT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_EXIT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_EXIT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_EXIT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_EXIT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_PROC_EXIT_V1 args {};
  args.exitCode = headerCopy.exitCode;

  (void)flags;
  const SyscallReturnCode ret = handle_Exit_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_AllocatePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_ALLOCATEPAGES_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_ALLOCATEPAGES_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_MEM_ALLOCATEPAGES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_MEM_ALLOCATEPAGES_V1 args {};
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const SyscallReturnCode ret = handle_AllocatePages_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_MEM_ALLOCATEPAGES_V1, address), &args.address, sizeof(args.address));
  return ret;
}

static uint32_t Dispatch_FreePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_FREEPAGES_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_FREEPAGES_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_MEM_FREEPAGES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_FREEPAGES_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_FREEPAGES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_FREEPAGES_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_MEM_FREEPAGES_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;

  (void)flags;
  const SyscallReturnCode ret = handle_FreePages_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Protect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_PROTECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_PROTECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_MEM_PROTECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_PROTECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_PROTECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_PROTECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_MEM_PROTECT_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const SyscallReturnCode ret = handle_Protect_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Open_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_OPEN_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_OPEN_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_FS_OPEN_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_OPEN_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_OPEN_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_OPEN_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.pathLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_FS_OPEN_V1 args {};
  if (headerCopy.path != nullptr && headerCopy.pathLength > 0) {
    args.path.resize(headerCopy.pathLength);
    if (args.path.data() == nullptr || args.path.size() != headerCopy.pathLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space)) {
      kstd::memcpy(args.path.data(), mapped, headerCopy.pathLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.pathLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  args.mode = headerCopy.mode;

  (void)flags;
  const SyscallReturnCode ret = handle_Open_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_FS_OPEN_V1, fileDescriptor), &args.fileDescriptor, sizeof(args.fileDescriptor));
  return ret;
}

static uint32_t Dispatch_Close_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_CLOSE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_CLOSE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_FS_CLOSE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_CLOSE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_CLOSE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_CLOSE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_FS_CLOSE_V1 args {};
  args.fileDescriptor = headerCopy.fileDescriptor;

  (void)flags;
  const SyscallReturnCode ret = handle_Close_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Read_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_READ_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_READ_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_FS_READ_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_READ_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_READ_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_READ_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_FS_READ_V1 args {};
  args.fileDescriptor = headerCopy.fileDescriptor;
  if (headerCopy.data != nullptr) {
    args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(KE_VMM_MapBorrowed(headerCopy.data, headerCopy.dataLength * sizeof(uint8_t), address_space)), headerCopy.dataLength, address_space);
    args.data_len = headerCopy.dataLength;
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Read_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.data_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_FS_READ_V1, dataLength), &len, sizeof(len));
  }
  return ret;
}

static uint32_t Dispatch_Write_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_WRITE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_WRITE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_FS_WRITE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_WRITE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_WRITE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_WRITE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_FS_WRITE_V1 args {};
  args.fileDescriptor = headerCopy.fileDescriptor;
  if (headerCopy.data != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.dataLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Write_V1(kstd::move(args));

  return ret;
}


UNDOS_KERNEL_API_DEF uint32_t KE_SYSCALL_Dispatch(uint32_t number, uint32_t size, void *data, uint32_t flags) noexcept {
  switch (number) {
    case 100: return Dispatch_Print_V1(data, size, flags);
    case 200: return Dispatch_CreateProcess_V1(data, size, flags);
    case 201: return Dispatch_GetInfo_V1(data, size, flags);
    case 202: return Dispatch_Exit_V1(data, size, flags);
    case 210: return Dispatch_AllocatePages_V1(data, size, flags);
    case 211: return Dispatch_FreePages_V1(data, size, flags);
    case 212: return Dispatch_Protect_V1(data, size, flags);
    case 220: return Dispatch_Open_V1(data, size, flags);
    case 221: return Dispatch_Close_V1(data, size, flags);
    case 222: return Dispatch_Read_V1(data, size, flags);
    case 223: return Dispatch_Write_V1(data, size, flags);
  }
  return SyscallReturnCode_UNKNOWN;
}


