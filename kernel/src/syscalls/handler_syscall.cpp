// Generated from UnDOS IDL

#include "stdkrn.hpp"

#include <memory.hpp>
#include <stddef.h>

#include "syscalls.hpp"

#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>


#include <undos/syscalls/__syscall_CreateProcess_V1.h>
#include <undos/syscalls/__syscall_GetInfo_V1.h>
#include <undos/syscalls/__syscall_Exit_V1.h>
#include <undos/syscalls/__syscall_AllocatePages_V1.h>
#include <undos/syscalls/__syscall_FreePages_V1.h>
#include <undos/syscalls/__syscall_Protect_V1.h>
#include <undos/syscalls/__syscall_Read_V1.h>
#include <undos/syscalls/__syscall_Write_V1.h>

constexpr size_t UNDOS_MAX_COPY_SIZE = 4096;
constexpr size_t UNDOS_MAX_MAPPED_BUFFER_SIZE = 64 * 1024 * 1024;

constexpr uint32_t SYSCALL_UNKNOWN = 0xFFFFFFFF;
constexpr uint32_t SYSCALL_INVALID_SIZE = 0xFFFFFFFE;
constexpr uint32_t SYSCALL_OUT_OF_MEMORY = 0xFFFFFFFD;

static void KE_VMM_CopyToUserspace(kernel::vmm::AddressSpace &as, void *userPtr, const void *kernelPtr, size_t length) {
  if (userPtr == nullptr || length == 0) [[unlikely]] return;
  void *mapped = KE_VMM_MapBorrowed(userPtr, length, as);
  if (mapped == nullptr) [[unlikely]] return;
  kstd::memcpy(mapped, kernelPtr, length);
  KE_VMM_UnmapBorrowed(mapped, length, as);
}

static uint32_t Dispatch_CreateProcess_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEPROCESS_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEPROCESS_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_PROC_CREATEPROCESS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space);
  }

  if (headerCopy.path != nullptr && headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return SYSCALL_INVALID_SIZE;
  if (headerCopy.argument != nullptr && headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return SYSCALL_INVALID_SIZE;

  const size_t total = sizeof(__SYS_PROC_CREATEPROCESS_V1) + (headerCopy.path != nullptr ? static_cast<size_t>(headerCopy.pathLength) + 1 : 0) + (headerCopy.argument != nullptr ? static_cast<size_t>(headerCopy.argumentLength) + 1 : 0);
  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(total));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_PROC_CREATEPROCESS_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_PROC_CREATEPROCESS_V1));
  size_t offset = sizeof(__SYS_PROC_CREATEPROCESS_V1);

  if (headerCopy.path != nullptr) {
    if (auto *src = static_cast<const char *>(KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space))) {
      kstd::memcpy(kbuf + offset, src, headerCopy.pathLength);
      kbuf[offset + headerCopy.pathLength] = 0;
      KE_VMM_UnmapBorrowed(const_cast<char *>(src), headerCopy.pathLength, address_space);
      kstruct->path = reinterpret_cast<const char *>(kbuf + offset);
      kstruct->pathLength = headerCopy.pathLength;
      offset += headerCopy.pathLength + 1;
    } else {
      KE_Free(kbuf);
      return SYSCALL_INVALID_SIZE;
    }
  }
  if (headerCopy.argument != nullptr) {
    if (auto *src = static_cast<const char *>(KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.argument), headerCopy.argumentLength, address_space))) {
      kstd::memcpy(kbuf + offset, src, headerCopy.argumentLength);
      kbuf[offset + headerCopy.argumentLength] = 0;
      KE_VMM_UnmapBorrowed(const_cast<char *>(src), headerCopy.argumentLength, address_space);
      kstruct->argument = reinterpret_cast<const char *>(kbuf + offset);
      kstruct->argumentLength = headerCopy.argumentLength;
      offset += headerCopy.argumentLength + 1;
    } else {
      KE_Free(kbuf);
      return SYSCALL_INVALID_SIZE;
    }
  }

  __KRN_PROC_CREATEPROCESS_V1 args {};
  if (kstruct->path != nullptr) args.path = kstd::string_view(kstruct->path, kstruct->pathLength);
  if (kstruct->argument != nullptr) args.argument = kstd::string_view(kstruct->argument, kstruct->argumentLength);
  args.flags = kstruct->flags;

  const uint32_t ret = handle_CreateProcess_V1(kstd::move(args), flags);


  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_GetInfo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_GETINFO_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_GETINFO_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_PROC_GETINFO_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_GETINFO_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_GETINFO_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_GETINFO_V1), address_space);
  }

  if (headerCopy.commandLine != nullptr && headerCopy.commandLineLength > UNDOS_MAX_MAPPED_BUFFER_SIZE) return SYSCALL_INVALID_SIZE;

  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_PROC_GETINFO_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_PROC_GETINFO_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_PROC_GETINFO_V1));

  __KRN_PROC_GETINFO_V1 args {};
  args.pid = kstruct->pid;
  if (headerCopy.commandLine != nullptr) {
    args.commandLine = kernel::borrowed_ptr<char>(static_cast<char *>(KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.commandLine), headerCopy.commandLineLength, address_space)), headerCopy.commandLineLength, address_space);
    args.commandLine_len = headerCopy.commandLineLength;
  }

  const uint32_t ret = handle_GetInfo_V1(kstd::move(args), flags);

  // content was written directly into the mapped user buffer; report the actual/required length.
  {
    const auto len = static_cast<uint32_t>(args.commandLine_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, commandLineLength), &len, sizeof(len));
  }
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, exitCode), &args.exitCode, sizeof(args.exitCode));

  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_Exit_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_EXIT_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_EXIT_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_PROC_EXIT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_EXIT_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_EXIT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_EXIT_V1), address_space);
  }


  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_PROC_EXIT_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_PROC_EXIT_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_PROC_EXIT_V1));

  __KRN_PROC_EXIT_V1 args {};
  args.exitCode = kstruct->exitCode;

  const uint32_t ret = handle_Exit_V1(kstd::move(args), flags);


  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_AllocatePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_ALLOCATEPAGES_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_ALLOCATEPAGES_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_MEM_ALLOCATEPAGES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space);
  }


  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_MEM_ALLOCATEPAGES_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_MEM_ALLOCATEPAGES_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_MEM_ALLOCATEPAGES_V1));

  __KRN_MEM_ALLOCATEPAGES_V1 args {};
  args.size = kstruct->size;
  args.flags = kstruct->flags;

  const uint32_t ret = handle_AllocatePages_V1(kstd::move(args), flags);

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_MEM_ALLOCATEPAGES_V1, address), &args.address, sizeof(args.address));

  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_FreePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_FREEPAGES_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_FREEPAGES_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_MEM_FREEPAGES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_FREEPAGES_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_FREEPAGES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_FREEPAGES_V1), address_space);
  }


  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_MEM_FREEPAGES_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_MEM_FREEPAGES_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_MEM_FREEPAGES_V1));

  __KRN_MEM_FREEPAGES_V1 args {};
  args.address = kstruct->address;
  args.size = kstruct->size;

  const uint32_t ret = handle_FreePages_V1(kstd::move(args), flags);


  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_Protect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_PROTECT_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_PROTECT_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_MEM_PROTECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_PROTECT_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_PROTECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_PROTECT_V1), address_space);
  }


  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_MEM_PROTECT_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_MEM_PROTECT_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_MEM_PROTECT_V1));

  __KRN_MEM_PROTECT_V1 args {};
  args.address = kstruct->address;
  args.size = kstruct->size;
  args.flags = kstruct->flags;

  const uint32_t ret = handle_Protect_V1(kstd::move(args), flags);


  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_Read_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_READ_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_READ_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_FS_READ_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_READ_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_READ_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_READ_V1), address_space);
  }

  if (headerCopy.data != nullptr && headerCopy.dataLength > UNDOS_MAX_MAPPED_BUFFER_SIZE) return SYSCALL_INVALID_SIZE;

  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_FS_READ_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_FS_READ_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_FS_READ_V1));

  __KRN_FS_READ_V1 args {};
  args.fileDescriptor = kstruct->fileDescriptor;
  if (headerCopy.data != nullptr) {
    args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(KE_VMM_MapBorrowed(headerCopy.data, headerCopy.dataLength * sizeof(uint8_t), address_space)), headerCopy.dataLength, address_space);
    args.data_len = headerCopy.dataLength;
  }

  const uint32_t ret = handle_Read_V1(kstd::move(args), flags);

  // buffer was mapped and filled by the kernel directly; report actual length written.
  { const auto len = static_cast<uint32_t>(args.data_len); KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_FS_READ_V1, dataLength), &len, sizeof(len)); }

  KE_Free(kbuf);
  return ret;
}

static uint32_t Dispatch_Write_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_FS_WRITE_V1)) return SYSCALL_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_FS_WRITE_V1 headerCopy {};
  {
    auto *mapped = static_cast<__SYS_FS_WRITE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_FS_WRITE_V1), address_space));
    if (mapped == nullptr) return SYSCALL_INVALID_SIZE;
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_FS_WRITE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_FS_WRITE_V1), address_space);
  }

  if (headerCopy.data != nullptr && headerCopy.dataLength > UNDOS_MAX_MAPPED_BUFFER_SIZE) return SYSCALL_INVALID_SIZE;

  auto *kbuf = static_cast<uint8_t *>(KE_Malloc(sizeof(__SYS_FS_WRITE_V1)));
  if (kbuf == nullptr) return SYSCALL_OUT_OF_MEMORY;
  auto *kstruct = reinterpret_cast<__SYS_FS_WRITE_V1 *>(kbuf);
  kstd::memcpy(kbuf, &headerCopy, sizeof(__SYS_FS_WRITE_V1));

  __KRN_FS_WRITE_V1 args {};
  args.fileDescriptor = kstruct->fileDescriptor;
  if (headerCopy.data != nullptr) {
    args.data = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space))), headerCopy.dataLength, address_space);
    args.data_len = headerCopy.dataLength;
  }

  const uint32_t ret = handle_Write_V1(kstd::move(args), flags);


  KE_Free(kbuf);
  return ret;
}


UNDOS_KERNEL_API_DEF uint32_t KE_SYSCALL_Dispatch(uint32_t number, uint32_t size, void *data, uint32_t flags) noexcept {
  switch (number) {
    case 200: return Dispatch_CreateProcess_V1(data, size, flags);
    case 201: return Dispatch_GetInfo_V1(data, size, flags);
    case 202: return Dispatch_Exit_V1(data, size, flags);
    case 210: return Dispatch_AllocatePages_V1(data, size, flags);
    case 211: return Dispatch_FreePages_V1(data, size, flags);
    case 212: return Dispatch_Protect_V1(data, size, flags);
    case 222: return Dispatch_Read_V1(data, size, flags);
    case 223: return Dispatch_Write_V1(data, size, flags);
  }
  return SYSCALL_UNKNOWN;
}


