// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#include "stdkrn.hpp"

#include <memory.hpp>
#include <stddef.h>

#include "syscalls.hpp"

#include <undos/syscalls/__enum_PixelFormat.h>
#include <undos/syscalls/__enum_RasterOp.h>
#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>

#include <undos/syscalls/__flags_DCInitFlags.h>
#include <undos/syscalls/__flags_PenStyle.h>
#include <undos/syscalls/__flags_BrushPattern.h>
#include <undos/syscalls/__flags_RegionCombineMode.h>
#include <undos/syscalls/__flags_Device3DFlags.h>
#include <undos/syscalls/__flags_FileOpenMode.h>

#include <undos/syscalls/__struct_Point.h>
#include <undos/syscalls/__struct_Rect.h>
#include <undos/syscalls/__struct_DeviceCaps.h>

#include <undos/syscalls/__syscall_CreateDC_V1.h>
#include <undos/syscalls/__syscall_DeleteDC_V1.h>
#include <undos/syscalls/__syscall_SelectObject_V1.h>
#include <undos/syscalls/__syscall_CreatePen_V1.h>
#include <undos/syscalls/__syscall_CreateBrush_V1.h>
#include <undos/syscalls/__syscall_CreateFont_V1.h>
#include <undos/syscalls/__syscall_CreateBitmap_V1.h>
#include <undos/syscalls/__syscall_DeleteObject_V1.h>
#include <undos/syscalls/__syscall_MoveTo_V1.h>
#include <undos/syscalls/__syscall_LineTo_V1.h>
#include <undos/syscalls/__syscall_Rectangle_V1.h>
#include <undos/syscalls/__syscall_Ellipse_V1.h>
#include <undos/syscalls/__syscall_TextOut_V1.h>
#include <undos/syscalls/__syscall_BitBlt_V1.h>
#include <undos/syscalls/__syscall_StretchBlt_V1.h>
#include <undos/syscalls/__syscall_AlphaBlend_V1.h>
#include <undos/syscalls/__syscall_CreateRegion_V1.h>
#include <undos/syscalls/__syscall_SelectClipRegion_V1.h>
#include <undos/syscalls/__syscall_CombineRegion_V1.h>
#include <undos/syscalls/__syscall_SetViewportOrg_V1.h>
#include <undos/syscalls/__syscall_SetWindowOrg_V1.h>
#include <undos/syscalls/__syscall_GetTextMetrics_V1.h>
#include <undos/syscalls/__syscall_GetBitmapBits_V1.h>
#include <undos/syscalls/__syscall_SetBitmapBits_V1.h>
#include <undos/syscalls/__syscall_GetDeviceCaps_V1.h>
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

static uint32_t Dispatch_CreateDC_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEDC_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEDC_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEDC_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEDC_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEDC_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEDC_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.driverName != nullptr) {
    if (headerCopy.driverNameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.driverNameLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  if (headerCopy.deviceName != nullptr) {
    if (headerCopy.deviceNameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.deviceNameLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_CREATEDC_V1 args {};
  if (headerCopy.driverName != nullptr && headerCopy.driverNameLength > 0) {
    args.driverName.resize(headerCopy.driverNameLength);
    if (args.driverName.data() == nullptr || args.driverName.size() != headerCopy.driverNameLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.driverName), headerCopy.driverNameLength, address_space)) {
      kstd::memcpy(args.driverName.data(), mapped, headerCopy.driverNameLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.driverNameLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  if (headerCopy.deviceName != nullptr && headerCopy.deviceNameLength > 0) {
    args.deviceName.resize(headerCopy.deviceNameLength);
    if (args.deviceName.data() == nullptr || args.deviceName.size() != headerCopy.deviceNameLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.deviceName), headerCopy.deviceNameLength, address_space)) {
      kstd::memcpy(args.deviceName.data(), mapped, headerCopy.deviceNameLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.deviceNameLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  args.initFlags = headerCopy.initFlags;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateDC_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_CREATEDC_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_DeleteDC_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_DELETEDC_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_DELETEDC_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_DELETEDC_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_DELETEDC_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_DELETEDC_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_DELETEDC_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_DELETEDC_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const SyscallReturnCode ret = handle_DeleteDC_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_SelectObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SELECTOBJECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SELECTOBJECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SELECTOBJECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SELECTOBJECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SELECTOBJECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SELECTOBJECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SELECTOBJECT_V1 args {};
  args.dc = headerCopy.dc;
  args.object = headerCopy.object;

  (void)flags;
  const SyscallReturnCode ret = handle_SelectObject_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreatePen_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEPEN_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEPEN_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEPEN_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEPEN_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEPEN_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEPEN_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_CREATEPEN_V1 args {};
  args.style = headerCopy.style;
  args.width = headerCopy.width;
  args.color = headerCopy.color;

  (void)flags;
  const SyscallReturnCode ret = handle_CreatePen_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateBrush_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEBRUSH_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEBRUSH_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEBRUSH_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEBRUSH_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEBRUSH_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEBRUSH_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_CREATEBRUSH_V1 args {};
  args.pattern = headerCopy.pattern;
  args.color = headerCopy.color;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateBrush_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateFont_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEFONT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEFONT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEFONT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEFONT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEFONT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEFONT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.name != nullptr) {
    if (headerCopy.nameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.nameLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_CREATEFONT_V1 args {};
  args.height = headerCopy.height;
  args.weight = headerCopy.weight;
  args.charset = headerCopy.charset;
  if (headerCopy.name != nullptr && headerCopy.nameLength > 0) {
    args.name.resize(headerCopy.nameLength);
    if (args.name.data() == nullptr || args.name.size() != headerCopy.nameLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.name), headerCopy.nameLength, address_space)) {
      kstd::memcpy(args.name.data(), mapped, headerCopy.nameLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.nameLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_CreateFont_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateBitmap_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEBITMAP_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEBITMAP_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEBITMAP_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEBITMAP_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEBITMAP_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEBITMAP_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.pixels != nullptr) {
    if (headerCopy.pixelsLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.pixelsLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_CREATEBITMAP_V1 args {};
  args.width = headerCopy.width;
  args.height = headerCopy.height;
  args.format = headerCopy.format;
  if (headerCopy.pixels != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.pixels), headerCopy.pixelsLength * sizeof(uint8_t), address_space)) {
      args.pixels = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.pixelsLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_CreateBitmap_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_CREATEBITMAP_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_DeleteObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_DELETEOBJECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_DELETEOBJECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_DELETEOBJECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_DELETEOBJECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_DELETEOBJECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_DELETEOBJECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_DELETEOBJECT_V1 args {};
  args.object = headerCopy.object;

  (void)flags;
  const SyscallReturnCode ret = handle_DeleteObject_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_MoveTo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_MOVETO_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_MOVETO_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_MOVETO_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_MOVETO_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_MOVETO_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_MOVETO_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_MOVETO_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const SyscallReturnCode ret = handle_MoveTo_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_LineTo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_LINETO_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_LINETO_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_LINETO_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_LINETO_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_LINETO_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_LINETO_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_LINETO_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const SyscallReturnCode ret = handle_LineTo_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Rectangle_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_RECTANGLE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_RECTANGLE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_RECTANGLE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_RECTANGLE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_RECTANGLE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_RECTANGLE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_RECTANGLE_V1 args {};
  args.dc = headerCopy.dc;
  args.left = headerCopy.left;
  args.top = headerCopy.top;
  args.right = headerCopy.right;
  args.bottom = headerCopy.bottom;

  (void)flags;
  const SyscallReturnCode ret = handle_Rectangle_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Ellipse_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ELLIPSE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ELLIPSE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_ELLIPSE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ELLIPSE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_ELLIPSE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_ELLIPSE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_ELLIPSE_V1 args {};
  args.dc = headerCopy.dc;
  args.left = headerCopy.left;
  args.top = headerCopy.top;
  args.right = headerCopy.right;
  args.bottom = headerCopy.bottom;

  (void)flags;
  const SyscallReturnCode ret = handle_Ellipse_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_TextOut_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_TEXTOUT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_TEXTOUT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_TEXTOUT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_TEXTOUT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_TEXTOUT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_TEXTOUT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.text != nullptr) {
    if (headerCopy.textLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.textLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_TEXTOUT_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  if (headerCopy.text != nullptr && headerCopy.textLength > 0) {
    args.text.resize(headerCopy.textLength);
    if (args.text.data() == nullptr || args.text.size() != headerCopy.textLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.text), headerCopy.textLength, address_space)) {
      kstd::memcpy(args.text.data(), mapped, headerCopy.textLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.textLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_TextOut_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_BitBlt_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_BITBLT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_BITBLT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_BITBLT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_BITBLT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_BITBLT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_BITBLT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_BITBLT_V1 args {};
  args.dstDC = headerCopy.dstDC;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  args.width = headerCopy.width;
  args.height = headerCopy.height;
  args.srcDC = headerCopy.srcDC;
  args.srcX = headerCopy.srcX;
  args.srcY = headerCopy.srcY;
  args.rop = headerCopy.rop;

  (void)flags;
  const SyscallReturnCode ret = handle_BitBlt_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_StretchBlt_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_STRETCHBLT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_STRETCHBLT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_STRETCHBLT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_STRETCHBLT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_STRETCHBLT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_STRETCHBLT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_STRETCHBLT_V1 args {};
  args.dstDC = headerCopy.dstDC;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  args.dstWidth = headerCopy.dstWidth;
  args.dstHeight = headerCopy.dstHeight;
  args.srcDC = headerCopy.srcDC;
  args.srcX = headerCopy.srcX;
  args.srcY = headerCopy.srcY;
  args.srcWidth = headerCopy.srcWidth;
  args.srcHeight = headerCopy.srcHeight;
  args.rop = headerCopy.rop;

  (void)flags;
  const SyscallReturnCode ret = handle_StretchBlt_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_AlphaBlend_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ALPHABLEND_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ALPHABLEND_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_ALPHABLEND_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ALPHABLEND_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_ALPHABLEND_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_ALPHABLEND_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_ALPHABLEND_V1 args {};
  args.dstDC = headerCopy.dstDC;
  args.srcDC = headerCopy.srcDC;
  args.dstRect = headerCopy.dstRect;
  args.srcRect = headerCopy.srcRect;
  args.alpha = headerCopy.alpha;

  (void)flags;
  const SyscallReturnCode ret = handle_AlphaBlend_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEREGION_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEREGION_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CREATEREGION_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEREGION_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CREATEREGION_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CREATEREGION_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.rects != nullptr) {
    if (headerCopy.rectsLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.rectsLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_CREATEREGION_V1 args {};
  if (headerCopy.rects != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<struct Rect *>(headerCopy.rects), headerCopy.rectsLength * sizeof(struct Rect), address_space)) {
      args.rects = kernel::borrowed_ptr<struct Rect>(const_cast<struct Rect *>(static_cast<const struct Rect *>(mapped)), headerCopy.rectsLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_CreateRegion_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_SelectClipRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SELECTCLIPREGION_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SELECTCLIPREGION_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SELECTCLIPREGION_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SELECTCLIPREGION_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SELECTCLIPREGION_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SELECTCLIPREGION_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SELECTCLIPREGION_V1 args {};
  args.dc = headerCopy.dc;
  args.region = headerCopy.region;

  (void)flags;
  const SyscallReturnCode ret = handle_SelectClipRegion_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CombineRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_COMBINEREGION_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_COMBINEREGION_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_COMBINEREGION_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_COMBINEREGION_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_COMBINEREGION_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_COMBINEREGION_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_COMBINEREGION_V1 args {};
  args.dest = headerCopy.dest;
  args.src1 = headerCopy.src1;
  args.src2 = headerCopy.src2;
  args.mode = headerCopy.mode;

  (void)flags;
  const SyscallReturnCode ret = handle_CombineRegion_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_SetViewportOrg_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETVIEWPORTORG_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETVIEWPORTORG_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SETVIEWPORTORG_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETVIEWPORTORG_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SETVIEWPORTORG_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SETVIEWPORTORG_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SETVIEWPORTORG_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const SyscallReturnCode ret = handle_SetViewportOrg_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_SetWindowOrg_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETWINDOWORG_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETWINDOWORG_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SETWINDOWORG_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETWINDOWORG_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SETWINDOWORG_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SETWINDOWORG_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SETWINDOWORG_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const SyscallReturnCode ret = handle_SetWindowOrg_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetTextMetrics_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETTEXTMETRICS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETTEXTMETRICS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_GETTEXTMETRICS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETTEXTMETRICS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_GETTEXTMETRICS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_GETTEXTMETRICS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_GETTEXTMETRICS_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const SyscallReturnCode ret = handle_GetTextMetrics_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETTEXTMETRICS_V1, metrics), &args.metrics, sizeof(args.metrics));
  return ret;
}

static uint32_t Dispatch_GetBitmapBits_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETBITMAPBITS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETBITMAPBITS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_GETBITMAPBITS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETBITMAPBITS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_GETBITMAPBITS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_GETBITMAPBITS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.buffer != nullptr) {
    if (headerCopy.bufferLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.bufferLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_GETBITMAPBITS_V1 args {};
  args.bitmap = headerCopy.bitmap;
  if (headerCopy.buffer != nullptr) {
    args.buffer = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(KE_VMM_MapBorrowed(headerCopy.buffer, headerCopy.bufferLength * sizeof(uint8_t), address_space)), headerCopy.bufferLength, address_space);
    args.buffer_len = headerCopy.bufferLength;
  }

  (void)flags;
  const SyscallReturnCode ret = handle_GetBitmapBits_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.buffer_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETBITMAPBITS_V1, bufferLength), &len, sizeof(len));
  }
  return ret;
}

static uint32_t Dispatch_SetBitmapBits_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETBITMAPBITS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETBITMAPBITS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SETBITMAPBITS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETBITMAPBITS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SETBITMAPBITS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SETBITMAPBITS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.buffer != nullptr) {
    if (headerCopy.bufferLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.bufferLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_SETBITMAPBITS_V1 args {};
  args.bitmap = headerCopy.bitmap;
  if (headerCopy.buffer != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.buffer), headerCopy.bufferLength * sizeof(uint8_t), address_space)) {
      args.buffer = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.bufferLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_SetBitmapBits_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetDeviceCaps_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETDEVICECAPS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETDEVICECAPS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_GETDEVICECAPS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETDEVICECAPS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_GETDEVICECAPS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_GETDEVICECAPS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_GETDEVICECAPS_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const SyscallReturnCode ret = handle_GetDeviceCaps_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETDEVICECAPS_V1, caps), &args.caps, sizeof(args.caps));
  return ret;
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
    case 300: return Dispatch_CreateDC_V1(data, size, flags);
    case 301: return Dispatch_DeleteDC_V1(data, size, flags);
    case 302: return Dispatch_SelectObject_V1(data, size, flags);
    case 303: return Dispatch_CreatePen_V1(data, size, flags);
    case 304: return Dispatch_CreateBrush_V1(data, size, flags);
    case 305: return Dispatch_CreateFont_V1(data, size, flags);
    case 306: return Dispatch_CreateBitmap_V1(data, size, flags);
    case 307: return Dispatch_DeleteObject_V1(data, size, flags);
    case 310: return Dispatch_MoveTo_V1(data, size, flags);
    case 311: return Dispatch_LineTo_V1(data, size, flags);
    case 312: return Dispatch_Rectangle_V1(data, size, flags);
    case 313: return Dispatch_Ellipse_V1(data, size, flags);
    case 314: return Dispatch_TextOut_V1(data, size, flags);
    case 320: return Dispatch_BitBlt_V1(data, size, flags);
    case 321: return Dispatch_StretchBlt_V1(data, size, flags);
    case 322: return Dispatch_AlphaBlend_V1(data, size, flags);
    case 330: return Dispatch_CreateRegion_V1(data, size, flags);
    case 331: return Dispatch_SelectClipRegion_V1(data, size, flags);
    case 332: return Dispatch_CombineRegion_V1(data, size, flags);
    case 333: return Dispatch_SetViewportOrg_V1(data, size, flags);
    case 334: return Dispatch_SetWindowOrg_V1(data, size, flags);
    case 340: return Dispatch_GetTextMetrics_V1(data, size, flags);
    case 341: return Dispatch_GetBitmapBits_V1(data, size, flags);
    case 342: return Dispatch_SetBitmapBits_V1(data, size, flags);
    case 343: return Dispatch_GetDeviceCaps_V1(data, size, flags);
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


