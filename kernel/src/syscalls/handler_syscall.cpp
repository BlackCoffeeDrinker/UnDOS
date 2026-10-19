// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#include "stdkrn.hpp"

#include <memory.hpp>
#include <stddef.h>

#include "syscalls.hpp"

#include <undos/syscalls/__enum_PixelFormat.h>
#include <undos/syscalls/__enum_RasterOp.h>
#include <undos/syscalls/__enum_WindowMessage.h>
#include <undos/syscalls/__enum_ShowCommand.h>
#include <undos/syscalls/__enum_HitTest.h>
#include <undos/syscalls/__enum_WindowLongIndex.h>
#include <undos/syscalls/__enum_FpuCapability.h>
#include <undos/syscalls/__enum_SyscallReturnCode.h>
#include <undos/syscalls/__enum_ProcessFlags.h>
#include <undos/syscalls/__enum_WaitTimeout.h>
#include <undos/syscalls/__enum_MemoryProtectFlags.h>
#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__enum_StdHandleSelector.h>

#include <undos/syscalls/__flags_DCInitFlags.h>
#include <undos/syscalls/__flags_PenStyle.h>
#include <undos/syscalls/__flags_BrushPattern.h>
#include <undos/syscalls/__flags_RegionCombineMode.h>
#include <undos/syscalls/__flags_Device3DFlags.h>
#include <undos/syscalls/__flags_WindowStyle.h>
#include <undos/syscalls/__flags_EventFlags.h>
#include <undos/syscalls/__flags_FileOpenMode.h>
#include <undos/syscalls/__flags_TtyModeFlags.h>

#include <undos/syscalls/__struct_Point.h>
#include <undos/syscalls/__struct_Rect.h>
#include <undos/syscalls/__struct_DeviceCaps.h>
#include <undos/syscalls/__struct_DisplayMode.h>
#include <undos/syscalls/__struct_DisplayState.h>
#include <undos/syscalls/__struct_TextMetrics.h>
#include <undos/syscalls/__struct_Msg.h>
#include <undos/syscalls/__struct_PaintInfo.h>
#include <undos/syscalls/__struct_IoctlDisplayMode.h>
#include <undos/syscalls/__struct_IoctlMouseConfig.h>
#include <undos/syscalls/__struct_IoctlGamepadState.h>
#include <undos/syscalls/__struct_IoctlGamepadCaps.h>

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
#include <undos/syscalls/__syscall_SetDrawColors_V1.h>
#include <undos/syscalls/__syscall_QueryFontEngine_V1.h>
#include <undos/syscalls/__syscall_BeginFontUpload_V1.h>
#include <undos/syscalls/__syscall_AppendFontUpload_V1.h>
#include <undos/syscalls/__syscall_CommitFontUpload_V1.h>
#include <undos/syscalls/__syscall_CloseFontResource_V1.h>
#include <undos/syscalls/__syscall_EnumDisplayModes_V1.h>
#include <undos/syscalls/__syscall_GetDisplayMode_V1.h>
#include <undos/syscalls/__syscall_SetDisplayMode_V1.h>
#include <undos/syscalls/__syscall_RegisterClass_V1.h>
#include <undos/syscalls/__syscall_CreateWindow_V1.h>
#include <undos/syscalls/__syscall_DestroyWindow_V1.h>
#include <undos/syscalls/__syscall_ShowWindow_V1.h>
#include <undos/syscalls/__syscall_MoveWindow_V1.h>
#include <undos/syscalls/__syscall_GetWindowRect_V1.h>
#include <undos/syscalls/__syscall_InvalidateRect_V1.h>
#include <undos/syscalls/__syscall_BeginPaint_V1.h>
#include <undos/syscalls/__syscall_EndPaint_V1.h>
#include <undos/syscalls/__syscall_GetMessage_V1.h>
#include <undos/syscalls/__syscall_PeekMessage_V1.h>
#include <undos/syscalls/__syscall_PostMessage_V1.h>
#include <undos/syscalls/__syscall_GetWindowLongPtr_V1.h>
#include <undos/syscalls/__syscall_SetWindowLongPtr_V1.h>
#include <undos/syscalls/__syscall_SetCapture_V1.h>
#include <undos/syscalls/__syscall_ReleaseCapture_V1.h>
#include <undos/syscalls/__syscall_GetClientRect_V1.h>
#include <undos/syscalls/__syscall_SetFocus_V1.h>
#include <undos/syscalls/__syscall_SetWindowText_V1.h>
#include <undos/syscalls/__syscall_QueryFpu_V1.h>
#include <undos/syscalls/__syscall_Print_V1.h>
#include <undos/syscalls/__syscall_CreateProcess_V1.h>
#include <undos/syscalls/__syscall_CreateProcess_V2.h>
#include <undos/syscalls/__syscall_GetInfo_V1.h>
#include <undos/syscalls/__syscall_Exit_V1.h>
#include <undos/syscalls/__syscall_WaitForObject_V1.h>
#include <undos/syscalls/__syscall_WaitProcess_V1.h>
#include <undos/syscalls/__syscall_CreateEvent_V1.h>
#include <undos/syscalls/__syscall_SetEvent_V1.h>
#include <undos/syscalls/__syscall_AllocatePages_V1.h>
#include <undos/syscalls/__syscall_FreePages_V1.h>
#include <undos/syscalls/__syscall_Protect_V1.h>
#include <undos/syscalls/__syscall_MapFixedPages_V1.h>
#include <undos/syscalls/__syscall_Open_V1.h>
#include <undos/syscalls/__syscall_Close_V1.h>
#include <undos/syscalls/__syscall_Read_V1.h>
#include <undos/syscalls/__syscall_Write_V1.h>
#include <undos/syscalls/__syscall_Write_V2.h>
#include <undos/syscalls/__syscall_Control_V1.h>
#include <undos/syscalls/__syscall_GetStdHandle_V1.h>
#include <undos/syscalls/__syscall_Create_V1.h>
#include <undos/syscalls/__syscall_SetConfig_V1.h>
#include <undos/syscalls/__syscall_GetConfig_V1.h>

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
  }

  if (headerCopy.deviceName != nullptr) {
    if (headerCopy.deviceNameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
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

static uint32_t Dispatch_SetDrawColors_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETDRAWCOLORS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETDRAWCOLORS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SETDRAWCOLORS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETDRAWCOLORS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SETDRAWCOLORS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SETDRAWCOLORS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SETDRAWCOLORS_V1 args {};
  args.dc = headerCopy.dc;
  args.foreground = headerCopy.foreground;
  args.background = headerCopy.background;
  args.opaque = headerCopy.opaque;

  (void)flags;
  const SyscallReturnCode ret = handle_SetDrawColors_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_QueryFontEngine_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_QUERYFONTENGINE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_QUERYFONTENGINE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_QUERYFONTENGINE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_QUERYFONTENGINE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_QUERYFONTENGINE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_QUERYFONTENGINE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_QUERYFONTENGINE_V1 args {};

  (void)flags;
  const SyscallReturnCode ret = handle_QueryFontEngine_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_QUERYFONTENGINE_V1, version), &args.version, sizeof(args.version));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_QUERYFONTENGINE_V1, capabilities), &args.capabilities, sizeof(args.capabilities));
  return ret;
}

static uint32_t Dispatch_BeginFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_BEGINFONTUPLOAD_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_BEGINFONTUPLOAD_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_BEGINFONTUPLOAD_V1 args {};
  args.totalBytes = headerCopy.totalBytes;

  (void)flags;
  const SyscallReturnCode ret = handle_BeginFontUpload_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_BEGINFONTUPLOAD_V1, upload), &args.upload, sizeof(args.upload));
  return ret;
}

static uint32_t Dispatch_AppendFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_APPENDFONTUPLOAD_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_APPENDFONTUPLOAD_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_GFX_APPENDFONTUPLOAD_V1 args {};
  args.upload = headerCopy.upload;
  args.offset = headerCopy.offset;
  if (headerCopy.data != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.dataLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_AppendFontUpload_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CommitFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_COMMITFONTUPLOAD_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_COMMITFONTUPLOAD_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_COMMITFONTUPLOAD_V1 args {};
  args.upload = headerCopy.upload;

  (void)flags;
  const SyscallReturnCode ret = handle_CommitFontUpload_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_COMMITFONTUPLOAD_V1, face), &args.face, sizeof(args.face));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_COMMITFONTUPLOAD_V1, validationError), &args.validationError, sizeof(args.validationError));
  return ret;
}

static uint32_t Dispatch_CloseFontResource_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CLOSEFONTRESOURCE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_CLOSEFONTRESOURCE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_CLOSEFONTRESOURCE_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const SyscallReturnCode ret = handle_CloseFontResource_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_EnumDisplayModes_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ENUMDISPLAYMODES_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_ENUMDISPLAYMODES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_ENUMDISPLAYMODES_V1 args {};
  args.index = headerCopy.index;

  (void)flags;
  const SyscallReturnCode ret = handle_EnumDisplayModes_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_ENUMDISPLAYMODES_V1, mode), &args.mode, sizeof(args.mode));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_ENUMDISPLAYMODES_V1, available), &args.available, sizeof(args.available));
  return ret;
}

static uint32_t Dispatch_GetDisplayMode_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETDISPLAYMODE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETDISPLAYMODE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_GETDISPLAYMODE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETDISPLAYMODE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_GETDISPLAYMODE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_GETDISPLAYMODE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_GETDISPLAYMODE_V1 args {};

  (void)flags;
  const SyscallReturnCode ret = handle_GetDisplayMode_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETDISPLAYMODE_V1, state), &args.state, sizeof(args.state));
  return ret;
}

static uint32_t Dispatch_SetDisplayMode_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETDISPLAYMODE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETDISPLAYMODE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_GFX_SETDISPLAYMODE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETDISPLAYMODE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_GFX_SETDISPLAYMODE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_GFX_SETDISPLAYMODE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_GFX_SETDISPLAYMODE_V1 args {};
  args.modeId = headerCopy.modeId;

  (void)flags;
  const SyscallReturnCode ret = handle_SetDisplayMode_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_RegisterClass_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_REGISTERCLASS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_REGISTERCLASS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_REGISTERCLASS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_REGISTERCLASS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_REGISTERCLASS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_REGISTERCLASS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.name != nullptr) {
    if (headerCopy.nameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  __KRN_USER_REGISTERCLASS_V1 args {};
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
  args.wndProc = headerCopy.wndProc;
  args.background = headerCopy.background;

  (void)flags;
  const SyscallReturnCode ret = handle_RegisterClass_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_CreateWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_CREATEWINDOW_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_CREATEWINDOW_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_CREATEWINDOW_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_CREATEWINDOW_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_CREATEWINDOW_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_CREATEWINDOW_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.className != nullptr) {
    if (headerCopy.classNameLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  if (headerCopy.title != nullptr) {
    if (headerCopy.titleLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  __KRN_USER_CREATEWINDOW_V1 args {};
  if (headerCopy.className != nullptr && headerCopy.classNameLength > 0) {
    args.className.resize(headerCopy.classNameLength);
    if (args.className.data() == nullptr || args.className.size() != headerCopy.classNameLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.className), headerCopy.classNameLength, address_space)) {
      kstd::memcpy(args.className.data(), mapped, headerCopy.classNameLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.classNameLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  if (headerCopy.title != nullptr && headerCopy.titleLength > 0) {
    args.title.resize(headerCopy.titleLength);
    if (args.title.data() == nullptr || args.title.size() != headerCopy.titleLength) return SyscallReturnCode_OUT_OF_MEMORY;
    if (auto *mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.title), headerCopy.titleLength, address_space)) {
      kstd::memcpy(args.title.data(), mapped, headerCopy.titleLength);
      KE_VMM_UnmapBorrowed(mapped, headerCopy.titleLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  args.style = headerCopy.style;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  args.width = headerCopy.width;
  args.height = headerCopy.height;
  args.parent = headerCopy.parent;
  args.userData = headerCopy.userData;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateWindow_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_CREATEWINDOW_V1, hwnd), &args.hwnd, sizeof(args.hwnd));
  return ret;
}

static uint32_t Dispatch_DestroyWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_DESTROYWINDOW_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_DESTROYWINDOW_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_DESTROYWINDOW_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_DESTROYWINDOW_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_DESTROYWINDOW_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_DESTROYWINDOW_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_DESTROYWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_DestroyWindow_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_ShowWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SHOWWINDOW_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SHOWWINDOW_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_SHOWWINDOW_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SHOWWINDOW_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_SHOWWINDOW_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_SHOWWINDOW_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_SHOWWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.command = headerCopy.command;

  (void)flags;
  const SyscallReturnCode ret = handle_ShowWindow_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_MoveWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_MOVEWINDOW_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_MOVEWINDOW_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_MOVEWINDOW_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_MOVEWINDOW_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_MOVEWINDOW_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_MOVEWINDOW_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_MOVEWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  args.width = headerCopy.width;
  args.height = headerCopy.height;

  (void)flags;
  const SyscallReturnCode ret = handle_MoveWindow_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetWindowRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETWINDOWRECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETWINDOWRECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_GETWINDOWRECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETWINDOWRECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_GETWINDOWRECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_GETWINDOWRECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_GETWINDOWRECT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_GetWindowRect_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETWINDOWRECT_V1, rect), &args.rect, sizeof(args.rect));
  return ret;
}

static uint32_t Dispatch_InvalidateRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_INVALIDATERECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_INVALIDATERECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_INVALIDATERECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_INVALIDATERECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_INVALIDATERECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_INVALIDATERECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_INVALIDATERECT_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.rect = headerCopy.rect;
  args.whole = headerCopy.whole;

  (void)flags;
  const SyscallReturnCode ret = handle_InvalidateRect_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_BeginPaint_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_BEGINPAINT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_BEGINPAINT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_BEGINPAINT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_BEGINPAINT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_BEGINPAINT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_BEGINPAINT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_BEGINPAINT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_BeginPaint_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_BEGINPAINT_V1, dc), &args.dc, sizeof(args.dc));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_BEGINPAINT_V1, paint), &args.paint, sizeof(args.paint));
  return ret;
}

static uint32_t Dispatch_EndPaint_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_ENDPAINT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_ENDPAINT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_ENDPAINT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_ENDPAINT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_ENDPAINT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_ENDPAINT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_ENDPAINT_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.dc = headerCopy.dc;

  (void)flags;
  const SyscallReturnCode ret = handle_EndPaint_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETMESSAGE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETMESSAGE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_GETMESSAGE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETMESSAGE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_GETMESSAGE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_GETMESSAGE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_GETMESSAGE_V1 args {};

  (void)flags;
  const SyscallReturnCode ret = handle_GetMessage_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETMESSAGE_V1, msg), &args.msg, sizeof(args.msg));
  return ret;
}

static uint32_t Dispatch_PeekMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_PEEKMESSAGE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_PEEKMESSAGE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_PEEKMESSAGE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_PEEKMESSAGE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_PEEKMESSAGE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_PEEKMESSAGE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_PEEKMESSAGE_V1 args {};
  args.remove = headerCopy.remove;

  (void)flags;
  const SyscallReturnCode ret = handle_PeekMessage_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_PEEKMESSAGE_V1, msg), &args.msg, sizeof(args.msg));
  return ret;
}

static uint32_t Dispatch_PostMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_POSTMESSAGE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_POSTMESSAGE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_POSTMESSAGE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_POSTMESSAGE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_POSTMESSAGE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_POSTMESSAGE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_POSTMESSAGE_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.message = headerCopy.message;
  args.wParam = headerCopy.wParam;
  args.lParam = headerCopy.lParam;

  (void)flags;
  const SyscallReturnCode ret = handle_PostMessage_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetWindowLongPtr_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETWINDOWLONGPTR_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETWINDOWLONGPTR_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_GETWINDOWLONGPTR_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_GETWINDOWLONGPTR_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.index = headerCopy.index;

  (void)flags;
  const SyscallReturnCode ret = handle_GetWindowLongPtr_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETWINDOWLONGPTR_V1, value), &args.value, sizeof(args.value));
  return ret;
}

static uint32_t Dispatch_SetWindowLongPtr_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETWINDOWLONGPTR_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETWINDOWLONGPTR_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_SETWINDOWLONGPTR_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_SETWINDOWLONGPTR_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.index = headerCopy.index;
  args.value = headerCopy.value;

  (void)flags;
  const SyscallReturnCode ret = handle_SetWindowLongPtr_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_SETWINDOWLONGPTR_V1, previous), &args.previous, sizeof(args.previous));
  return ret;
}

static uint32_t Dispatch_SetCapture_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETCAPTURE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETCAPTURE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_SETCAPTURE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETCAPTURE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_SETCAPTURE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_SETCAPTURE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_SETCAPTURE_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_SetCapture_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_ReleaseCapture_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_RELEASECAPTURE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_RELEASECAPTURE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_RELEASECAPTURE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_RELEASECAPTURE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_RELEASECAPTURE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_RELEASECAPTURE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_RELEASECAPTURE_V1 args {};
  args.unused = headerCopy.unused;

  (void)flags;
  const SyscallReturnCode ret = handle_ReleaseCapture_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetClientRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETCLIENTRECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETCLIENTRECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_GETCLIENTRECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETCLIENTRECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_GETCLIENTRECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_GETCLIENTRECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_GETCLIENTRECT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_GetClientRect_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETCLIENTRECT_V1, rect), &args.rect, sizeof(args.rect));
  return ret;
}

static uint32_t Dispatch_SetFocus_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETFOCUS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETFOCUS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_SETFOCUS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETFOCUS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_SETFOCUS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_SETFOCUS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_USER_SETFOCUS_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const SyscallReturnCode ret = handle_SetFocus_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_SetWindowText_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETWINDOWTEXT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETWINDOWTEXT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_USER_SETWINDOWTEXT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETWINDOWTEXT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_USER_SETWINDOWTEXT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_USER_SETWINDOWTEXT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.text != nullptr) {
    if (headerCopy.textLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  __KRN_USER_SETWINDOWTEXT_V1 args {};
  args.hwnd = headerCopy.hwnd;
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
  const SyscallReturnCode ret = handle_SetWindowText_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_QueryFpu_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_ABI_QUERYFPU_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_ABI_QUERYFPU_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_ABI_QUERYFPU_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_ABI_QUERYFPU_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_ABI_QUERYFPU_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_ABI_QUERYFPU_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_ABI_QUERYFPU_V1 args {};

  (void)flags;
  const SyscallReturnCode ret = handle_QueryFpu_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_ABI_QUERYFPU_V1, capability), &args.capability, sizeof(args.capability));
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
  }

  if (headerCopy.argument != nullptr) {
    if (headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
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

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEPROCESS_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_CreateProcess_V2(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEPROCESS_V2)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEPROCESS_V2 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_CREATEPROCESS_V2 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEPROCESS_V2), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_CREATEPROCESS_V2));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_CREATEPROCESS_V2), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  if (headerCopy.argument != nullptr) {
    if (headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  __KRN_PROC_CREATEPROCESS_V2 args {};
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
  args.stdinHandle = headerCopy.stdinHandle;
  args.stdoutHandle = headerCopy.stdoutHandle;
  args.stderrHandle = headerCopy.stderrHandle;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateProcess_V2(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEPROCESS_V2, handle), &args.handle, sizeof(args.handle));
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

static uint32_t Dispatch_WaitForObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_WAITFOROBJECT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_WAITFOROBJECT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_WAITFOROBJECT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_WAITFOROBJECT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_WAITFOROBJECT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_WAITFOROBJECT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_PROC_WAITFOROBJECT_V1 args {};
  args.handle = headerCopy.handle;
  args.timeoutMs = headerCopy.timeoutMs;

  (void)flags;
  const SyscallReturnCode ret = handle_WaitForObject_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_WaitProcess_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_WAITPROCESS_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_WAITPROCESS_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_WAITPROCESS_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_WAITPROCESS_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_WAITPROCESS_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_WAITPROCESS_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_PROC_WAITPROCESS_V1 args {};
  args.handle = headerCopy.handle;
  args.timeoutMs = headerCopy.timeoutMs;

  (void)flags;
  const SyscallReturnCode ret = handle_WaitProcess_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_WAITPROCESS_V1, exitCode), &args.exitCode, sizeof(args.exitCode));
  return ret;
}

static uint32_t Dispatch_CreateEvent_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEEVENT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEEVENT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_CREATEEVENT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEEVENT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_CREATEEVENT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_CREATEEVENT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_PROC_CREATEEVENT_V1 args {};
  args.flags = headerCopy.flags;

  (void)flags;
  const SyscallReturnCode ret = handle_CreateEvent_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEEVENT_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_SetEvent_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_SETEVENT_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_SETEVENT_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_PROC_SETEVENT_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_SETEVENT_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_PROC_SETEVENT_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_PROC_SETEVENT_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_PROC_SETEVENT_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const SyscallReturnCode ret = handle_SetEvent_V1(kstd::move(args));

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

static uint32_t Dispatch_MapFixedPages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_MAPFIXEDPAGES_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_MAPFIXEDPAGES_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_MEM_MAPFIXEDPAGES_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_MEM_MAPFIXEDPAGES_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const SyscallReturnCode ret = handle_MapFixedPages_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Open_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_OPEN_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_OPEN_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_OPEN_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_OPEN_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_OPEN_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_OPEN_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
  }

  __KRN_IO_OPEN_V1 args {};
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

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_OPEN_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_Close_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_CLOSE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_CLOSE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_CLOSE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_CLOSE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_CLOSE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_CLOSE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_IO_CLOSE_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const SyscallReturnCode ret = handle_Close_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_Read_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_READ_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_READ_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_READ_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_READ_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_READ_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_READ_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_IO_READ_V1 args {};
  args.handle = headerCopy.handle;
  if (headerCopy.data != nullptr) {
    args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(KE_VMM_MapBorrowed(headerCopy.data, headerCopy.dataLength * sizeof(uint8_t), address_space)), headerCopy.dataLength, address_space);
    args.data_len = headerCopy.dataLength;
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Read_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.data_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_READ_V1, dataLength), &len, sizeof(len));
  }
  return ret;
}

static uint32_t Dispatch_Write_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_WRITE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_WRITE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_WRITE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_WRITE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_WRITE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_WRITE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_IO_WRITE_V1 args {};
  args.handle = headerCopy.handle;
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

static uint32_t Dispatch_Write_V2(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_WRITE_V2)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_WRITE_V2 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_WRITE_V2 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_WRITE_V2), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_WRITE_V2));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_WRITE_V2), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.dataLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_IO_WRITE_V2 args {};
  args.handle = headerCopy.handle;
  if (headerCopy.data != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.dataLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Write_V2(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_WRITE_V2, transferred), &args.transferred, sizeof(args.transferred));
  return ret;
}

static uint32_t Dispatch_Control_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_CONTROL_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_CONTROL_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_CONTROL_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_CONTROL_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_CONTROL_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_CONTROL_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  if (headerCopy.input != nullptr) {
    if (headerCopy.inputLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.inputLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  if (headerCopy.output != nullptr) {
    if (headerCopy.outputLength > UNDOS_MAX_COPY_SIZE) return SyscallReturnCode_INVALID_SIZE;
    if (headerCopy.outputLength == 0) return SyscallReturnCode_INVALID_ARGUMENT;
  }

  __KRN_IO_CONTROL_V1 args {};
  args.handle = headerCopy.handle;
  args.code = headerCopy.code;
  if (headerCopy.input != nullptr) {
    if (auto* mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.input), headerCopy.inputLength * sizeof(uint8_t), address_space)) {
      args.input = kernel::borrowed_ptr<uint8_t>(const_cast<uint8_t *>(static_cast<const uint8_t *>(mapped)), headerCopy.inputLength, address_space);
    } else {
      return SyscallReturnCode_INVALID_ADDRESS;
    }
  }
  if (headerCopy.output != nullptr) {
    args.output = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(KE_VMM_MapBorrowed(headerCopy.output, headerCopy.outputLength * sizeof(uint8_t), address_space)), headerCopy.outputLength, address_space);
    args.output_len = headerCopy.outputLength;
  }

  (void)flags;
  const SyscallReturnCode ret = handle_Control_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.output_len);
    KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_CONTROL_V1, outputLength), &len, sizeof(len));
  }
  return ret;
}

static uint32_t Dispatch_GetStdHandle_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_GETSTDHANDLE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_GETSTDHANDLE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_IO_GETSTDHANDLE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_GETSTDHANDLE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_IO_GETSTDHANDLE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_IO_GETSTDHANDLE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_IO_GETSTDHANDLE_V1 args {};
  args.selector = headerCopy.selector;

  (void)flags;
  const SyscallReturnCode ret = handle_GetStdHandle_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_GETSTDHANDLE_V1, handle), &args.handle, sizeof(args.handle));
  return ret;
}

static uint32_t Dispatch_Create_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_CREATE_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_CREATE_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_TTY_CREATE_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_CREATE_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_TTY_CREATE_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_TTY_CREATE_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_TTY_CREATE_V1 args {};

  (void)flags;
  const SyscallReturnCode ret = handle_Create_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_CREATE_V1, sourceHandle), &args.sourceHandle, sizeof(args.sourceHandle));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_CREATE_V1, sinkHandle), &args.sinkHandle, sizeof(args.sinkHandle));
  return ret;
}

static uint32_t Dispatch_SetConfig_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_SETCONFIG_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_SETCONFIG_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_TTY_SETCONFIG_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_SETCONFIG_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_TTY_SETCONFIG_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_TTY_SETCONFIG_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_TTY_SETCONFIG_V1 args {};
  args.handle = headerCopy.handle;
  args.flags = headerCopy.flags;
  args.rows = headerCopy.rows;
  args.cols = headerCopy.cols;

  (void)flags;
  const SyscallReturnCode ret = handle_SetConfig_V1(kstd::move(args));

  return ret;
}

static uint32_t Dispatch_GetConfig_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_GETCONFIG_V1)) return SyscallReturnCode_INVALID_SIZE;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_GETCONFIG_V1 headerCopy {};
  if (auto* mapped = static_cast<__SYS_TTY_GETCONFIG_V1 *>(KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_GETCONFIG_V1), address_space))) {
    kstd::memcpy(&headerCopy, mapped, sizeof(__SYS_TTY_GETCONFIG_V1));
    KE_VMM_UnmapBorrowed(mapped, sizeof(__SYS_TTY_GETCONFIG_V1), address_space);
  } else {
    return SyscallReturnCode_INVALID_ADDRESS;
  }

  __KRN_TTY_GETCONFIG_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const SyscallReturnCode ret = handle_GetConfig_V1(kstd::move(args));

  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, flags), &args.flags, sizeof(args.flags));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, rows), &args.rows, sizeof(args.rows));
  KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, cols), &args.cols, sizeof(args.cols));
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
    case 344: return Dispatch_SetDrawColors_V1(data, size, flags);
    case 345: return Dispatch_QueryFontEngine_V1(data, size, flags);
    case 346: return Dispatch_BeginFontUpload_V1(data, size, flags);
    case 347: return Dispatch_AppendFontUpload_V1(data, size, flags);
    case 348: return Dispatch_CommitFontUpload_V1(data, size, flags);
    case 349: return Dispatch_CloseFontResource_V1(data, size, flags);
    case 350: return Dispatch_EnumDisplayModes_V1(data, size, flags);
    case 351: return Dispatch_GetDisplayMode_V1(data, size, flags);
    case 352: return Dispatch_SetDisplayMode_V1(data, size, flags);
    case 400: return Dispatch_RegisterClass_V1(data, size, flags);
    case 401: return Dispatch_CreateWindow_V1(data, size, flags);
    case 402: return Dispatch_DestroyWindow_V1(data, size, flags);
    case 403: return Dispatch_ShowWindow_V1(data, size, flags);
    case 404: return Dispatch_MoveWindow_V1(data, size, flags);
    case 405: return Dispatch_GetWindowRect_V1(data, size, flags);
    case 406: return Dispatch_InvalidateRect_V1(data, size, flags);
    case 407: return Dispatch_BeginPaint_V1(data, size, flags);
    case 408: return Dispatch_EndPaint_V1(data, size, flags);
    case 409: return Dispatch_GetMessage_V1(data, size, flags);
    case 410: return Dispatch_PeekMessage_V1(data, size, flags);
    case 411: return Dispatch_PostMessage_V1(data, size, flags);
    case 412: return Dispatch_GetWindowLongPtr_V1(data, size, flags);
    case 413: return Dispatch_SetWindowLongPtr_V1(data, size, flags);
    case 414: return Dispatch_SetCapture_V1(data, size, flags);
    case 415: return Dispatch_ReleaseCapture_V1(data, size, flags);
    case 416: return Dispatch_GetClientRect_V1(data, size, flags);
    case 417: return Dispatch_SetFocus_V1(data, size, flags);
    case 418: return Dispatch_SetWindowText_V1(data, size, flags);
    case 207: return Dispatch_QueryFpu_V1(data, size, flags);
    case 100: return Dispatch_Print_V1(data, size, flags);
    case 200: return Dispatch_CreateProcess_V1(data, size, flags);
    case 208: return Dispatch_CreateProcess_V2(data, size, flags);
    case 201: return Dispatch_GetInfo_V1(data, size, flags);
    case 202: return Dispatch_Exit_V1(data, size, flags);
    case 203: return Dispatch_WaitForObject_V1(data, size, flags);
    case 204: return Dispatch_WaitProcess_V1(data, size, flags);
    case 205: return Dispatch_CreateEvent_V1(data, size, flags);
    case 206: return Dispatch_SetEvent_V1(data, size, flags);
    case 210: return Dispatch_AllocatePages_V1(data, size, flags);
    case 211: return Dispatch_FreePages_V1(data, size, flags);
    case 212: return Dispatch_Protect_V1(data, size, flags);
    case 213: return Dispatch_MapFixedPages_V1(data, size, flags);
    case 220: return Dispatch_Open_V1(data, size, flags);
    case 221: return Dispatch_Close_V1(data, size, flags);
    case 222: return Dispatch_Read_V1(data, size, flags);
    case 223: return Dispatch_Write_V1(data, size, flags);
    case 226: return Dispatch_Write_V2(data, size, flags);
    case 224: return Dispatch_Control_V1(data, size, flags);
    case 225: return Dispatch_GetStdHandle_V1(data, size, flags);
    case 250: return Dispatch_Create_V1(data, size, flags);
    case 251: return Dispatch_SetConfig_V1(data, size, flags);
    case 252: return Dispatch_GetConfig_V1(data, size, flags);
  }
  return SyscallReturnCode_UNKNOWN;
}


