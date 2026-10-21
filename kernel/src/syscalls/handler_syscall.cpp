// Generated from UnDOS IDL

// See (tools/create-syscalls.py)
#include "stdkrn.hpp"

#include <memory.hpp>
#include <stddef.h>

#include "syscalls.hpp"
#include "KErrorToSysReturnCode.hpp"

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
#include <undos/syscalls/__syscall_QueryNetworkVersion_V1.h>
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

static kstd::error_code KE_VMM_CopyToUserspace(kernel::vmm::AddressSpace &as, void *userPtr, const void *kernelPtr, size_t length) {
  if (length == 0) return kernel::KernelError::Success;
  if (userPtr == nullptr) [[unlikely]] return kernel::KernelError::InvalidAddress;
  const auto mapped = KE_VMM_MapBorrowed(userPtr, length, as);
  if (!mapped) [[unlikely]] return mapped.error();
  kstd::memcpy(*mapped, kernelPtr, length);
  return KE_VMM_UnmapBorrowed(*mapped, length, as);
}

// One slot per module-provided syscall, indexed at generation time so dispatch is a
// single load and null check. An empty slot means the provider is not loaded.
namespace {
struct ProviderSlot {
  kstd::string_view provider;
  kstd::string_view symbol;
  kernel::VirtualAddress handler;
};

ProviderSlot g_providerSlots[] = {
    {"network", "handle_QueryNetworkVersion_V1", kernel::VirtualAddress{0}},
};
}// namespace

namespace kernel::syscall {
kstd::error_code BindProvider(const KObjectPtr<KModuleObject> &module) {
  const kstd::string_view moduleName(module->name);
  for (const auto &slot: g_providerSlots) {
    if (slot.provider != moduleName) continue;
    if (slot.handler.value != 0) return KernelError::AlreadyExists;
    if (!module->exports.Find(slot.symbol)) return KernelError::NotFound;
  }
  for (auto &slot: g_providerSlots) {
    if (slot.provider != moduleName) continue;
    slot.handler = *module->exports.Find(slot.symbol);
  }
  return KernelError::Success;
}
}// namespace kernel::syscall

static kstd::error_code Dispatch_CreateDC_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEDC_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEDC_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEDC_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEDC_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEDC_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.driverName != nullptr) {
    if (headerCopy.driverNameLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  if (headerCopy.deviceName != nullptr) {
    if (headerCopy.deviceNameLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_GFX_CREATEDC_V1 args {};
  if (headerCopy.driverName != nullptr && headerCopy.driverNameLength > 0) {
    args.driverName.resize(headerCopy.driverNameLength);
    if (args.driverName.data() == nullptr || args.driverName.size() != headerCopy.driverNameLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.driverName), headerCopy.driverNameLength, address_space)) {
      kstd::memcpy(args.driverName.data(), *mapped, headerCopy.driverNameLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.driverNameLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  if (headerCopy.deviceName != nullptr && headerCopy.deviceNameLength > 0) {
    args.deviceName.resize(headerCopy.deviceNameLength);
    if (args.deviceName.data() == nullptr || args.deviceName.size() != headerCopy.deviceNameLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.deviceName), headerCopy.deviceNameLength, address_space)) {
      kstd::memcpy(args.deviceName.data(), *mapped, headerCopy.deviceNameLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.deviceNameLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  args.initFlags = headerCopy.initFlags;

  (void)flags;
  const kstd::error_code ret = handle_CreateDC_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_CREATEDC_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_DeleteDC_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_DELETEDC_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_DELETEDC_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_DELETEDC_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_DELETEDC_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_DELETEDC_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_DELETEDC_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const kstd::error_code ret = handle_DeleteDC_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_SelectObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SELECTOBJECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SELECTOBJECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SELECTOBJECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SELECTOBJECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SELECTOBJECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SELECTOBJECT_V1 args {};
  args.dc = headerCopy.dc;
  args.object = headerCopy.object;

  (void)flags;
  const kstd::error_code ret = handle_SelectObject_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreatePen_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEPEN_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEPEN_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEPEN_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEPEN_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEPEN_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_CREATEPEN_V1 args {};
  args.style = headerCopy.style;
  args.width = headerCopy.width;
  args.color = headerCopy.color;

  (void)flags;
  const kstd::error_code ret = handle_CreatePen_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateBrush_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEBRUSH_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEBRUSH_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEBRUSH_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEBRUSH_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEBRUSH_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_CREATEBRUSH_V1 args {};
  args.pattern = headerCopy.pattern;
  args.color = headerCopy.color;

  (void)flags;
  const kstd::error_code ret = handle_CreateBrush_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateFont_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEFONT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEFONT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEFONT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEFONT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEFONT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.name != nullptr) {
    if (headerCopy.nameLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_GFX_CREATEFONT_V1 args {};
  args.height = headerCopy.height;
  args.weight = headerCopy.weight;
  args.charset = headerCopy.charset;
  if (headerCopy.name != nullptr && headerCopy.nameLength > 0) {
    args.name.resize(headerCopy.nameLength);
    if (args.name.data() == nullptr || args.name.size() != headerCopy.nameLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.name), headerCopy.nameLength, address_space)) {
      kstd::memcpy(args.name.data(), *mapped, headerCopy.nameLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.nameLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_CreateFont_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateBitmap_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEBITMAP_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEBITMAP_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEBITMAP_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEBITMAP_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEBITMAP_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.pixels != nullptr) {
    if (headerCopy.pixelsLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.pixelsLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_GFX_CREATEBITMAP_V1 args {};
  args.width = headerCopy.width;
  args.height = headerCopy.height;
  args.format = headerCopy.format;
  if (headerCopy.pixels != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.pixels), headerCopy.pixelsLength * sizeof(uint8_t), address_space)) {
      args.pixels = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.pixelsLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_CreateBitmap_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_CREATEBITMAP_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_DeleteObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_DELETEOBJECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_DELETEOBJECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_DELETEOBJECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_DELETEOBJECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_DELETEOBJECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_DELETEOBJECT_V1 args {};
  args.object = headerCopy.object;

  (void)flags;
  const kstd::error_code ret = handle_DeleteObject_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_MoveTo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_MOVETO_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_MOVETO_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_MOVETO_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_MOVETO_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_MOVETO_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_MOVETO_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const kstd::error_code ret = handle_MoveTo_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_LineTo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_LINETO_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_LINETO_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_LINETO_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_LINETO_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_LINETO_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_LINETO_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const kstd::error_code ret = handle_LineTo_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Rectangle_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_RECTANGLE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_RECTANGLE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_RECTANGLE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_RECTANGLE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_RECTANGLE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_RECTANGLE_V1 args {};
  args.dc = headerCopy.dc;
  args.left = headerCopy.left;
  args.top = headerCopy.top;
  args.right = headerCopy.right;
  args.bottom = headerCopy.bottom;

  (void)flags;
  const kstd::error_code ret = handle_Rectangle_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Ellipse_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ELLIPSE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ELLIPSE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ELLIPSE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_ELLIPSE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_ELLIPSE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_ELLIPSE_V1 args {};
  args.dc = headerCopy.dc;
  args.left = headerCopy.left;
  args.top = headerCopy.top;
  args.right = headerCopy.right;
  args.bottom = headerCopy.bottom;

  (void)flags;
  const kstd::error_code ret = handle_Ellipse_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_TextOut_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_TEXTOUT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_TEXTOUT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_TEXTOUT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_TEXTOUT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_TEXTOUT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.text != nullptr) {
    if (headerCopy.textLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_GFX_TEXTOUT_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  if (headerCopy.text != nullptr && headerCopy.textLength > 0) {
    args.text.resize(headerCopy.textLength);
    if (args.text.data() == nullptr || args.text.size() != headerCopy.textLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.text), headerCopy.textLength, address_space)) {
      kstd::memcpy(args.text.data(), *mapped, headerCopy.textLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.textLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_TextOut_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_BitBlt_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_BITBLT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_BITBLT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_BITBLT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_BITBLT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_BITBLT_V1), address_space); error) return error;
  } else {
    return mapped.error();
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
  const kstd::error_code ret = handle_BitBlt_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_StretchBlt_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_STRETCHBLT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_STRETCHBLT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_STRETCHBLT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_STRETCHBLT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_STRETCHBLT_V1), address_space); error) return error;
  } else {
    return mapped.error();
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
  const kstd::error_code ret = handle_StretchBlt_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_AlphaBlend_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ALPHABLEND_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ALPHABLEND_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ALPHABLEND_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_ALPHABLEND_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_ALPHABLEND_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_ALPHABLEND_V1 args {};
  args.dstDC = headerCopy.dstDC;
  args.srcDC = headerCopy.srcDC;
  args.dstRect = headerCopy.dstRect;
  args.srcRect = headerCopy.srcRect;
  args.alpha = headerCopy.alpha;

  (void)flags;
  const kstd::error_code ret = handle_AlphaBlend_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CREATEREGION_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CREATEREGION_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CREATEREGION_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CREATEREGION_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CREATEREGION_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.rects != nullptr) {
    if (headerCopy.rectsLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.rectsLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_GFX_CREATEREGION_V1 args {};
  if (headerCopy.rects != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<struct Rect *>(headerCopy.rects), headerCopy.rectsLength * sizeof(struct Rect), address_space)) {
      args.rects = kernel::borrowed_ptr<struct Rect>(static_cast<struct Rect *>(*mapped), headerCopy.rectsLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_CreateRegion_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_SelectClipRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SELECTCLIPREGION_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SELECTCLIPREGION_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SELECTCLIPREGION_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SELECTCLIPREGION_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SELECTCLIPREGION_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SELECTCLIPREGION_V1 args {};
  args.dc = headerCopy.dc;
  args.region = headerCopy.region;

  (void)flags;
  const kstd::error_code ret = handle_SelectClipRegion_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CombineRegion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_COMBINEREGION_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_COMBINEREGION_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_COMBINEREGION_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_COMBINEREGION_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_COMBINEREGION_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_COMBINEREGION_V1 args {};
  args.dest = headerCopy.dest;
  args.src1 = headerCopy.src1;
  args.src2 = headerCopy.src2;
  args.mode = headerCopy.mode;

  (void)flags;
  const kstd::error_code ret = handle_CombineRegion_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_SetViewportOrg_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETVIEWPORTORG_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETVIEWPORTORG_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETVIEWPORTORG_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SETVIEWPORTORG_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SETVIEWPORTORG_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SETVIEWPORTORG_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const kstd::error_code ret = handle_SetViewportOrg_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_SetWindowOrg_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETWINDOWORG_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETWINDOWORG_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETWINDOWORG_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SETWINDOWORG_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SETWINDOWORG_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SETWINDOWORG_V1 args {};
  args.dc = headerCopy.dc;
  args.x = headerCopy.x;
  args.y = headerCopy.y;

  (void)flags;
  const kstd::error_code ret = handle_SetWindowOrg_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetTextMetrics_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETTEXTMETRICS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETTEXTMETRICS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETTEXTMETRICS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_GETTEXTMETRICS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_GETTEXTMETRICS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_GETTEXTMETRICS_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const kstd::error_code ret = handle_GetTextMetrics_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETTEXTMETRICS_V1, metrics), &args.metrics, sizeof(args.metrics)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_GetBitmapBits_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETBITMAPBITS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETBITMAPBITS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETBITMAPBITS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_GETBITMAPBITS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_GETBITMAPBITS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.buffer != nullptr) {
    if (headerCopy.bufferLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.bufferLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_GFX_GETBITMAPBITS_V1 args {};
  args.bitmap = headerCopy.bitmap;
  if (headerCopy.buffer != nullptr) {
    const auto mapped = KE_VMM_MapBorrowed(headerCopy.buffer, headerCopy.bufferLength * sizeof(uint8_t), address_space);
    if (!mapped) return mapped.error();
    args.buffer = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.bufferLength, address_space);
    args.buffer_len = headerCopy.bufferLength;
  }

  (void)flags;
  const kstd::error_code ret = handle_GetBitmapBits_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.buffer_len);
    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETBITMAPBITS_V1, bufferLength), &len, sizeof(len)); error) return error;
  }
  return ret;
}

static kstd::error_code Dispatch_SetBitmapBits_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETBITMAPBITS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETBITMAPBITS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETBITMAPBITS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SETBITMAPBITS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SETBITMAPBITS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.buffer != nullptr) {
    if (headerCopy.bufferLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.bufferLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_GFX_SETBITMAPBITS_V1 args {};
  args.bitmap = headerCopy.bitmap;
  if (headerCopy.buffer != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.buffer), headerCopy.bufferLength * sizeof(uint8_t), address_space)) {
      args.buffer = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.bufferLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_SetBitmapBits_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetDeviceCaps_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETDEVICECAPS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETDEVICECAPS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETDEVICECAPS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_GETDEVICECAPS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_GETDEVICECAPS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_GETDEVICECAPS_V1 args {};
  args.dc = headerCopy.dc;

  (void)flags;
  const kstd::error_code ret = handle_GetDeviceCaps_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETDEVICECAPS_V1, caps), &args.caps, sizeof(args.caps)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetDrawColors_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETDRAWCOLORS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETDRAWCOLORS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETDRAWCOLORS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SETDRAWCOLORS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SETDRAWCOLORS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SETDRAWCOLORS_V1 args {};
  args.dc = headerCopy.dc;
  args.foreground = headerCopy.foreground;
  args.background = headerCopy.background;
  args.opaque = headerCopy.opaque;

  (void)flags;
  const kstd::error_code ret = handle_SetDrawColors_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_QueryFontEngine_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_QUERYFONTENGINE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_QUERYFONTENGINE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_QUERYFONTENGINE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_QUERYFONTENGINE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_QUERYFONTENGINE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_QUERYFONTENGINE_V1 args {};

  (void)flags;
  const kstd::error_code ret = handle_QueryFontEngine_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_QUERYFONTENGINE_V1, version), &args.version, sizeof(args.version)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_QUERYFONTENGINE_V1, capabilities), &args.capabilities, sizeof(args.capabilities)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_BeginFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_BEGINFONTUPLOAD_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_BEGINFONTUPLOAD_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_BEGINFONTUPLOAD_V1 args {};
  args.totalBytes = headerCopy.totalBytes;

  (void)flags;
  const kstd::error_code ret = handle_BeginFontUpload_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_BEGINFONTUPLOAD_V1, upload), &args.upload, sizeof(args.upload)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_AppendFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_APPENDFONTUPLOAD_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_APPENDFONTUPLOAD_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.dataLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_GFX_APPENDFONTUPLOAD_V1 args {};
  args.upload = headerCopy.upload;
  args.offset = headerCopy.offset;
  if (headerCopy.data != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.dataLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_AppendFontUpload_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CommitFontUpload_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_COMMITFONTUPLOAD_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_COMMITFONTUPLOAD_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_COMMITFONTUPLOAD_V1 args {};
  args.upload = headerCopy.upload;

  (void)flags;
  const kstd::error_code ret = handle_CommitFontUpload_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_COMMITFONTUPLOAD_V1, face), &args.face, sizeof(args.face)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_COMMITFONTUPLOAD_V1, validationError), &args.validationError, sizeof(args.validationError)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_CloseFontResource_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_CLOSEFONTRESOURCE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_CLOSEFONTRESOURCE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_CLOSEFONTRESOURCE_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const kstd::error_code ret = handle_CloseFontResource_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_EnumDisplayModes_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_ENUMDISPLAYMODES_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_ENUMDISPLAYMODES_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_ENUMDISPLAYMODES_V1 args {};
  args.index = headerCopy.index;

  (void)flags;
  const kstd::error_code ret = handle_EnumDisplayModes_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_ENUMDISPLAYMODES_V1, mode), &args.mode, sizeof(args.mode)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_ENUMDISPLAYMODES_V1, available), &args.available, sizeof(args.available)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_GetDisplayMode_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_GETDISPLAYMODE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_GETDISPLAYMODE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_GETDISPLAYMODE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_GETDISPLAYMODE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_GETDISPLAYMODE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_GETDISPLAYMODE_V1 args {};

  (void)flags;
  const kstd::error_code ret = handle_GetDisplayMode_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_GFX_GETDISPLAYMODE_V1, state), &args.state, sizeof(args.state)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetDisplayMode_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_GFX_SETDISPLAYMODE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_GFX_SETDISPLAYMODE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_GFX_SETDISPLAYMODE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_GFX_SETDISPLAYMODE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_GFX_SETDISPLAYMODE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_GFX_SETDISPLAYMODE_V1 args {};
  args.modeId = headerCopy.modeId;

  (void)flags;
  const kstd::error_code ret = handle_SetDisplayMode_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_RegisterClass_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_REGISTERCLASS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_REGISTERCLASS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_REGISTERCLASS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_REGISTERCLASS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_REGISTERCLASS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.name != nullptr) {
    if (headerCopy.nameLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_USER_REGISTERCLASS_V1 args {};
  if (headerCopy.name != nullptr && headerCopy.nameLength > 0) {
    args.name.resize(headerCopy.nameLength);
    if (args.name.data() == nullptr || args.name.size() != headerCopy.nameLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.name), headerCopy.nameLength, address_space)) {
      kstd::memcpy(args.name.data(), *mapped, headerCopy.nameLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.nameLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  args.wndProc = headerCopy.wndProc;
  args.background = headerCopy.background;

  (void)flags;
  const kstd::error_code ret = handle_RegisterClass_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_CREATEWINDOW_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_CREATEWINDOW_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_CREATEWINDOW_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_CREATEWINDOW_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_CREATEWINDOW_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.className != nullptr) {
    if (headerCopy.classNameLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  if (headerCopy.title != nullptr) {
    if (headerCopy.titleLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_USER_CREATEWINDOW_V1 args {};
  if (headerCopy.className != nullptr && headerCopy.classNameLength > 0) {
    args.className.resize(headerCopy.classNameLength);
    if (args.className.data() == nullptr || args.className.size() != headerCopy.classNameLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.className), headerCopy.classNameLength, address_space)) {
      kstd::memcpy(args.className.data(), *mapped, headerCopy.classNameLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.classNameLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  if (headerCopy.title != nullptr && headerCopy.titleLength > 0) {
    args.title.resize(headerCopy.titleLength);
    if (args.title.data() == nullptr || args.title.size() != headerCopy.titleLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.title), headerCopy.titleLength, address_space)) {
      kstd::memcpy(args.title.data(), *mapped, headerCopy.titleLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.titleLength, address_space); error) return error;
    } else {
      return mapped.error();
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
  const kstd::error_code ret = handle_CreateWindow_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_CREATEWINDOW_V1, hwnd), &args.hwnd, sizeof(args.hwnd)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_DestroyWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_DESTROYWINDOW_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_DESTROYWINDOW_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_DESTROYWINDOW_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_DESTROYWINDOW_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_DESTROYWINDOW_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_DESTROYWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_DestroyWindow_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_ShowWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SHOWWINDOW_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SHOWWINDOW_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SHOWWINDOW_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_SHOWWINDOW_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_SHOWWINDOW_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_SHOWWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.command = headerCopy.command;

  (void)flags;
  const kstd::error_code ret = handle_ShowWindow_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_MoveWindow_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_MOVEWINDOW_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_MOVEWINDOW_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_MOVEWINDOW_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_MOVEWINDOW_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_MOVEWINDOW_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_MOVEWINDOW_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.x = headerCopy.x;
  args.y = headerCopy.y;
  args.width = headerCopy.width;
  args.height = headerCopy.height;

  (void)flags;
  const kstd::error_code ret = handle_MoveWindow_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetWindowRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETWINDOWRECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETWINDOWRECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETWINDOWRECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_GETWINDOWRECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_GETWINDOWRECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_GETWINDOWRECT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_GetWindowRect_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETWINDOWRECT_V1, rect), &args.rect, sizeof(args.rect)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_InvalidateRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_INVALIDATERECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_INVALIDATERECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_INVALIDATERECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_INVALIDATERECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_INVALIDATERECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_INVALIDATERECT_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.rect = headerCopy.rect;
  args.whole = headerCopy.whole;

  (void)flags;
  const kstd::error_code ret = handle_InvalidateRect_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_BeginPaint_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_BEGINPAINT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_BEGINPAINT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_BEGINPAINT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_BEGINPAINT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_BEGINPAINT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_BEGINPAINT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_BeginPaint_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_BEGINPAINT_V1, dc), &args.dc, sizeof(args.dc)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_BEGINPAINT_V1, paint), &args.paint, sizeof(args.paint)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_EndPaint_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_ENDPAINT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_ENDPAINT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_ENDPAINT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_ENDPAINT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_ENDPAINT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_ENDPAINT_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.dc = headerCopy.dc;

  (void)flags;
  const kstd::error_code ret = handle_EndPaint_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETMESSAGE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETMESSAGE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETMESSAGE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_GETMESSAGE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_GETMESSAGE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_GETMESSAGE_V1 args {};

  (void)flags;
  const kstd::error_code ret = handle_GetMessage_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETMESSAGE_V1, msg), &args.msg, sizeof(args.msg)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_PeekMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_PEEKMESSAGE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_PEEKMESSAGE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_PEEKMESSAGE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_PEEKMESSAGE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_PEEKMESSAGE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_PEEKMESSAGE_V1 args {};
  args.remove = headerCopy.remove;

  (void)flags;
  const kstd::error_code ret = handle_PeekMessage_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_PEEKMESSAGE_V1, msg), &args.msg, sizeof(args.msg)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_PostMessage_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_POSTMESSAGE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_POSTMESSAGE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_POSTMESSAGE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_POSTMESSAGE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_POSTMESSAGE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_POSTMESSAGE_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.message = headerCopy.message;
  args.wParam = headerCopy.wParam;
  args.lParam = headerCopy.lParam;

  (void)flags;
  const kstd::error_code ret = handle_PostMessage_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetWindowLongPtr_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETWINDOWLONGPTR_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETWINDOWLONGPTR_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_GETWINDOWLONGPTR_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_GETWINDOWLONGPTR_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.index = headerCopy.index;

  (void)flags;
  const kstd::error_code ret = handle_GetWindowLongPtr_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETWINDOWLONGPTR_V1, value), &args.value, sizeof(args.value)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetWindowLongPtr_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETWINDOWLONGPTR_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETWINDOWLONGPTR_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_SETWINDOWLONGPTR_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_SETWINDOWLONGPTR_V1 args {};
  args.hwnd = headerCopy.hwnd;
  args.index = headerCopy.index;
  args.value = headerCopy.value;

  (void)flags;
  const kstd::error_code ret = handle_SetWindowLongPtr_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_SETWINDOWLONGPTR_V1, previous), &args.previous, sizeof(args.previous)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetCapture_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETCAPTURE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETCAPTURE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETCAPTURE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_SETCAPTURE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_SETCAPTURE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_SETCAPTURE_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_SetCapture_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_ReleaseCapture_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_RELEASECAPTURE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_RELEASECAPTURE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_RELEASECAPTURE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_RELEASECAPTURE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_RELEASECAPTURE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_RELEASECAPTURE_V1 args {};
  args.unused = headerCopy.unused;

  (void)flags;
  const kstd::error_code ret = handle_ReleaseCapture_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetClientRect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_GETCLIENTRECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_GETCLIENTRECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_GETCLIENTRECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_GETCLIENTRECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_GETCLIENTRECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_GETCLIENTRECT_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_GetClientRect_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_USER_GETCLIENTRECT_V1, rect), &args.rect, sizeof(args.rect)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetFocus_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETFOCUS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETFOCUS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETFOCUS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_SETFOCUS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_SETFOCUS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_USER_SETFOCUS_V1 args {};
  args.hwnd = headerCopy.hwnd;

  (void)flags;
  const kstd::error_code ret = handle_SetFocus_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_SetWindowText_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_USER_SETWINDOWTEXT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_USER_SETWINDOWTEXT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_USER_SETWINDOWTEXT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_USER_SETWINDOWTEXT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_USER_SETWINDOWTEXT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.text != nullptr) {
    if (headerCopy.textLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_USER_SETWINDOWTEXT_V1 args {};
  args.hwnd = headerCopy.hwnd;
  if (headerCopy.text != nullptr && headerCopy.textLength > 0) {
    args.text.resize(headerCopy.textLength);
    if (args.text.data() == nullptr || args.text.size() != headerCopy.textLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.text), headerCopy.textLength, address_space)) {
      kstd::memcpy(args.text.data(), *mapped, headerCopy.textLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.textLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_SetWindowText_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_QueryFpu_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_ABI_QUERYFPU_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_ABI_QUERYFPU_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_ABI_QUERYFPU_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_ABI_QUERYFPU_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_ABI_QUERYFPU_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_ABI_QUERYFPU_V1 args {};

  (void)flags;
  const kstd::error_code ret = handle_QueryFpu_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_ABI_QUERYFPU_V1, capability), &args.capability, sizeof(args.capability)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_QueryNetworkVersion_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_NET_QUERYNETWORKVERSION_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_NET_QUERYNETWORKVERSION_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_NET_QUERYNETWORKVERSION_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_NET_QUERYNETWORKVERSION_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_NET_QUERYNETWORKVERSION_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_NET_QUERYNETWORKVERSION_V1 args {};

  (void)flags;
  const auto handler = g_providerSlots[0].handler;
  if (handler.value == 0) return kernel::KernelError::Unsupported;
  const kstd::error_code ret = handler.as_ptr<kstd::error_code(__KRN_NET_QUERYNETWORKVERSION_V1 &&)>()(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_NET_QUERYNETWORKVERSION_V1, version), &args.version, sizeof(args.version)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_Print_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_DEBUG_PRINT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_DEBUG_PRINT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_DEBUG_PRINT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_DEBUG_PRINT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_DEBUG_PRINT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.message != nullptr) {
    if (headerCopy.messageLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_DEBUG_PRINT_V1 args {};
  if (headerCopy.message != nullptr && headerCopy.messageLength > 0) {
    args.message.resize(headerCopy.messageLength);
    if (args.message.data() == nullptr || args.message.size() != headerCopy.messageLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.message), headerCopy.messageLength, address_space)) {
      kstd::memcpy(args.message.data(), *mapped, headerCopy.messageLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.messageLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_Print_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_CreateProcess_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEPROCESS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEPROCESS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_CREATEPROCESS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  if (headerCopy.argument != nullptr) {
    if (headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_PROC_CREATEPROCESS_V1 args {};
  if (headerCopy.path != nullptr && headerCopy.pathLength > 0) {
    args.path.resize(headerCopy.pathLength);
    if (args.path.data() == nullptr || args.path.size() != headerCopy.pathLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space)) {
      kstd::memcpy(args.path.data(), *mapped, headerCopy.pathLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.pathLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  if (headerCopy.argument != nullptr && headerCopy.argumentLength > 0) {
    args.argument.resize(headerCopy.argumentLength);
    if (args.argument.data() == nullptr || args.argument.size() != headerCopy.argumentLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.argument), headerCopy.argumentLength, address_space)) {
      kstd::memcpy(args.argument.data(), *mapped, headerCopy.argumentLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.argumentLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  args.flags = headerCopy.flags;

  (void)flags;
  const kstd::error_code ret = handle_CreateProcess_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEPROCESS_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_CreateProcess_V2(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEPROCESS_V2)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEPROCESS_V2 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEPROCESS_V2), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_CREATEPROCESS_V2));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_CREATEPROCESS_V2), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  if (headerCopy.argument != nullptr) {
    if (headerCopy.argumentLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_PROC_CREATEPROCESS_V2 args {};
  if (headerCopy.path != nullptr && headerCopy.pathLength > 0) {
    args.path.resize(headerCopy.pathLength);
    if (args.path.data() == nullptr || args.path.size() != headerCopy.pathLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space)) {
      kstd::memcpy(args.path.data(), *mapped, headerCopy.pathLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.pathLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  if (headerCopy.argument != nullptr && headerCopy.argumentLength > 0) {
    args.argument.resize(headerCopy.argumentLength);
    if (args.argument.data() == nullptr || args.argument.size() != headerCopy.argumentLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.argument), headerCopy.argumentLength, address_space)) {
      kstd::memcpy(args.argument.data(), *mapped, headerCopy.argumentLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.argumentLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  args.flags = headerCopy.flags;
  args.stdinHandle = headerCopy.stdinHandle;
  args.stdoutHandle = headerCopy.stdoutHandle;
  args.stderrHandle = headerCopy.stderrHandle;

  (void)flags;
  const kstd::error_code ret = handle_CreateProcess_V2(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEPROCESS_V2, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_GetInfo_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_GETINFO_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_GETINFO_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_GETINFO_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_GETINFO_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_GETINFO_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.commandLine != nullptr) {
    if (headerCopy.commandLineLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.commandLineLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_PROC_GETINFO_V1 args {};
  args.pid = headerCopy.pid;
  if (headerCopy.commandLine != nullptr) {
    const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.commandLine), headerCopy.commandLineLength, address_space);
    if (!mapped) return mapped.error();
    args.commandLine = kernel::borrowed_ptr<char>(static_cast<char *>(*mapped), headerCopy.commandLineLength, address_space);
    args.commandLine_len = headerCopy.commandLineLength;
  }

  (void)flags;
  const kstd::error_code ret = handle_GetInfo_V1(kstd::move(args));

  // content was written directly into the mapped user buffer; report the actual/required length.
  {
    const auto len = static_cast<uint32_t>(args.commandLine_len);
    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, commandLineLength), &len, sizeof(len)); error) return error;
  }
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_GETINFO_V1, exitCode), &args.exitCode, sizeof(args.exitCode)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_Exit_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_EXIT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_EXIT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_EXIT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_EXIT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_EXIT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_PROC_EXIT_V1 args {};
  args.exitCode = headerCopy.exitCode;

  (void)flags;
  const kstd::error_code ret = handle_Exit_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_WaitForObject_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_WAITFOROBJECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_WAITFOROBJECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_WAITFOROBJECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_WAITFOROBJECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_WAITFOROBJECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_PROC_WAITFOROBJECT_V1 args {};
  args.handle = headerCopy.handle;
  args.timeoutMs = headerCopy.timeoutMs;

  (void)flags;
  const kstd::error_code ret = handle_WaitForObject_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_WaitProcess_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_WAITPROCESS_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_WAITPROCESS_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_WAITPROCESS_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_WAITPROCESS_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_WAITPROCESS_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_PROC_WAITPROCESS_V1 args {};
  args.handle = headerCopy.handle;
  args.timeoutMs = headerCopy.timeoutMs;

  (void)flags;
  const kstd::error_code ret = handle_WaitProcess_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_WAITPROCESS_V1, exitCode), &args.exitCode, sizeof(args.exitCode)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_CreateEvent_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_CREATEEVENT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_CREATEEVENT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_CREATEEVENT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_CREATEEVENT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_CREATEEVENT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_PROC_CREATEEVENT_V1 args {};
  args.flags = headerCopy.flags;

  (void)flags;
  const kstd::error_code ret = handle_CreateEvent_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_PROC_CREATEEVENT_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetEvent_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_PROC_SETEVENT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_PROC_SETEVENT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_PROC_SETEVENT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_PROC_SETEVENT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_PROC_SETEVENT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_PROC_SETEVENT_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const kstd::error_code ret = handle_SetEvent_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_AllocatePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_ALLOCATEPAGES_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_ALLOCATEPAGES_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_MEM_ALLOCATEPAGES_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_MEM_ALLOCATEPAGES_V1 args {};
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const kstd::error_code ret = handle_AllocatePages_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_MEM_ALLOCATEPAGES_V1, address), &args.address, sizeof(args.address)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_FreePages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_FREEPAGES_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_FREEPAGES_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_FREEPAGES_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_MEM_FREEPAGES_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_MEM_FREEPAGES_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_MEM_FREEPAGES_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;

  (void)flags;
  const kstd::error_code ret = handle_FreePages_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Protect_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_PROTECT_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_PROTECT_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_PROTECT_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_MEM_PROTECT_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_MEM_PROTECT_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_MEM_PROTECT_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const kstd::error_code ret = handle_Protect_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_MapFixedPages_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_MEM_MAPFIXEDPAGES_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_MEM_MAPFIXEDPAGES_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_MEM_MAPFIXEDPAGES_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_MEM_MAPFIXEDPAGES_V1 args {};
  args.address = headerCopy.address;
  args.size = headerCopy.size;
  args.flags = headerCopy.flags;

  (void)flags;
  const kstd::error_code ret = handle_MapFixedPages_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Open_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_OPEN_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_OPEN_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_OPEN_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_OPEN_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_OPEN_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.path != nullptr) {
    if (headerCopy.pathLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
  }

  __KRN_IO_OPEN_V1 args {};
  if (headerCopy.path != nullptr && headerCopy.pathLength > 0) {
    args.path.resize(headerCopy.pathLength);
    if (args.path.data() == nullptr || args.path.size() != headerCopy.pathLength) return kernel::KernelError::OutOfMemory;
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<char *>(headerCopy.path), headerCopy.pathLength, address_space)) {
      kstd::memcpy(args.path.data(), *mapped, headerCopy.pathLength);
      if (const auto error = KE_VMM_UnmapBorrowed(*mapped, headerCopy.pathLength, address_space); error) return error;
    } else {
      return mapped.error();
    }
  }
  args.mode = headerCopy.mode;

  (void)flags;
  const kstd::error_code ret = handle_Open_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_OPEN_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_Close_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_CLOSE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_CLOSE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_CLOSE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_CLOSE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_CLOSE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_IO_CLOSE_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const kstd::error_code ret = handle_Close_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Read_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_READ_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_READ_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_READ_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_READ_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_READ_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.dataLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_IO_READ_V1 args {};
  args.handle = headerCopy.handle;
  if (headerCopy.data != nullptr) {
    const auto mapped = KE_VMM_MapBorrowed(headerCopy.data, headerCopy.dataLength * sizeof(uint8_t), address_space);
    if (!mapped) return mapped.error();
    args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.dataLength, address_space);
    args.data_len = headerCopy.dataLength;
  }

  (void)flags;
  const kstd::error_code ret = handle_Read_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.data_len);
    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_READ_V1, dataLength), &len, sizeof(len)); error) return error;
  }
  return ret;
}

static kstd::error_code Dispatch_Write_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_WRITE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_WRITE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_WRITE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_WRITE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_WRITE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.dataLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_IO_WRITE_V1 args {};
  args.handle = headerCopy.handle;
  if (headerCopy.data != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.dataLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_Write_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_Write_V2(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_WRITE_V2)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_WRITE_V2 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_WRITE_V2), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_WRITE_V2));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_WRITE_V2), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.data != nullptr) {
    if (headerCopy.dataLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.dataLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_IO_WRITE_V2 args {};
  args.handle = headerCopy.handle;
  if (headerCopy.data != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.data), headerCopy.dataLength * sizeof(uint8_t), address_space)) {
      args.data = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.dataLength, address_space);
    } else {
      return mapped.error();
    }
  }

  (void)flags;
  const kstd::error_code ret = handle_Write_V2(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_WRITE_V2, transferred), &args.transferred, sizeof(args.transferred)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_Control_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_CONTROL_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_CONTROL_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_CONTROL_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_CONTROL_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_CONTROL_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  if (headerCopy.input != nullptr) {
    if (headerCopy.inputLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.inputLength == 0) return kernel::KernelError::InvalidArgument;
  }

  if (headerCopy.output != nullptr) {
    if (headerCopy.outputLength > UNDOS_MAX_COPY_SIZE) return kernel::KernelError::InvalidSize;
    if (headerCopy.outputLength == 0) return kernel::KernelError::InvalidArgument;
  }

  __KRN_IO_CONTROL_V1 args {};
  args.handle = headerCopy.handle;
  args.code = headerCopy.code;
  if (headerCopy.input != nullptr) {
    if (const auto mapped = KE_VMM_MapBorrowed(const_cast<uint8_t *>(headerCopy.input), headerCopy.inputLength * sizeof(uint8_t), address_space)) {
      args.input = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.inputLength, address_space);
    } else {
      return mapped.error();
    }
  }
  if (headerCopy.output != nullptr) {
    const auto mapped = KE_VMM_MapBorrowed(headerCopy.output, headerCopy.outputLength * sizeof(uint8_t), address_space);
    if (!mapped) return mapped.error();
    args.output = kernel::borrowed_ptr<uint8_t>(static_cast<uint8_t *>(*mapped), headerCopy.outputLength, address_space);
    args.output_len = headerCopy.outputLength;
  }

  (void)flags;
  const kstd::error_code ret = handle_Control_V1(kstd::move(args));

  // buffer was mapped and filled by the kernel directly; report actual length written.
  {
    const auto len = static_cast<uint32_t>(args.output_len);
    if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_CONTROL_V1, outputLength), &len, sizeof(len)); error) return error;
  }
  return ret;
}

static kstd::error_code Dispatch_GetStdHandle_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_IO_GETSTDHANDLE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_IO_GETSTDHANDLE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_IO_GETSTDHANDLE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_IO_GETSTDHANDLE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_IO_GETSTDHANDLE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_IO_GETSTDHANDLE_V1 args {};
  args.selector = headerCopy.selector;

  (void)flags;
  const kstd::error_code ret = handle_GetStdHandle_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_IO_GETSTDHANDLE_V1, handle), &args.handle, sizeof(args.handle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_Create_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_CREATE_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_CREATE_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_CREATE_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_TTY_CREATE_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_TTY_CREATE_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_TTY_CREATE_V1 args {};

  (void)flags;
  const kstd::error_code ret = handle_Create_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_CREATE_V1, sourceHandle), &args.sourceHandle, sizeof(args.sourceHandle)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_CREATE_V1, sinkHandle), &args.sinkHandle, sizeof(args.sinkHandle)); error) return error;
  return ret;
}

static kstd::error_code Dispatch_SetConfig_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_SETCONFIG_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_SETCONFIG_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_SETCONFIG_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_TTY_SETCONFIG_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_TTY_SETCONFIG_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_TTY_SETCONFIG_V1 args {};
  args.handle = headerCopy.handle;
  args.flags = headerCopy.flags;
  args.rows = headerCopy.rows;
  args.cols = headerCopy.cols;

  (void)flags;
  const kstd::error_code ret = handle_SetConfig_V1(kstd::move(args));

  return ret;
}

static kstd::error_code Dispatch_GetConfig_V1(void *userData, size_t size, uint32_t flags) {
  if (size < sizeof(__SYS_TTY_GETCONFIG_V1)) return kernel::KernelError::InvalidSize;

  const auto currentThread = KE_SCHED_GetCurrentThread();
  if (!currentThread) return kernel::KernelError::NoCurrentThread;
  if (!currentThread->owner_process) return kernel::KernelError::NoCurrentProcess;
  auto &address_space = currentThread->owner_process->address_space;

  __SYS_TTY_GETCONFIG_V1 headerCopy {};
  if (const auto mapped = KE_VMM_MapBorrowed(userData, sizeof(__SYS_TTY_GETCONFIG_V1), address_space)) {
    kstd::memcpy(&headerCopy, *mapped, sizeof(__SYS_TTY_GETCONFIG_V1));
    if (const auto error = KE_VMM_UnmapBorrowed(*mapped, sizeof(__SYS_TTY_GETCONFIG_V1), address_space); error) return error;
  } else {
    return mapped.error();
  }

  __KRN_TTY_GETCONFIG_V1 args {};
  args.handle = headerCopy.handle;

  (void)flags;
  const kstd::error_code ret = handle_GetConfig_V1(kstd::move(args));

  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, flags), &args.flags, sizeof(args.flags)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, rows), &args.rows, sizeof(args.rows)); error) return error;
  if (const auto error = KE_VMM_CopyToUserspace(address_space, static_cast<char *>(userData) + offsetof(__SYS_TTY_GETCONFIG_V1, cols), &args.cols, sizeof(args.cols)); error) return error;
  return ret;
}


UNDOS_KERNEL_API_DEF uint32_t KE_SYSCALL_Dispatch(uint32_t number, uint32_t size, void *data, uint32_t flags) noexcept {
  switch (number) {
    case 300: return kernel::KErrorToSysReturnCode(Dispatch_CreateDC_V1(data, size, flags));
    case 301: return kernel::KErrorToSysReturnCode(Dispatch_DeleteDC_V1(data, size, flags));
    case 302: return kernel::KErrorToSysReturnCode(Dispatch_SelectObject_V1(data, size, flags));
    case 303: return kernel::KErrorToSysReturnCode(Dispatch_CreatePen_V1(data, size, flags));
    case 304: return kernel::KErrorToSysReturnCode(Dispatch_CreateBrush_V1(data, size, flags));
    case 305: return kernel::KErrorToSysReturnCode(Dispatch_CreateFont_V1(data, size, flags));
    case 306: return kernel::KErrorToSysReturnCode(Dispatch_CreateBitmap_V1(data, size, flags));
    case 307: return kernel::KErrorToSysReturnCode(Dispatch_DeleteObject_V1(data, size, flags));
    case 310: return kernel::KErrorToSysReturnCode(Dispatch_MoveTo_V1(data, size, flags));
    case 311: return kernel::KErrorToSysReturnCode(Dispatch_LineTo_V1(data, size, flags));
    case 312: return kernel::KErrorToSysReturnCode(Dispatch_Rectangle_V1(data, size, flags));
    case 313: return kernel::KErrorToSysReturnCode(Dispatch_Ellipse_V1(data, size, flags));
    case 314: return kernel::KErrorToSysReturnCode(Dispatch_TextOut_V1(data, size, flags));
    case 320: return kernel::KErrorToSysReturnCode(Dispatch_BitBlt_V1(data, size, flags));
    case 321: return kernel::KErrorToSysReturnCode(Dispatch_StretchBlt_V1(data, size, flags));
    case 322: return kernel::KErrorToSysReturnCode(Dispatch_AlphaBlend_V1(data, size, flags));
    case 330: return kernel::KErrorToSysReturnCode(Dispatch_CreateRegion_V1(data, size, flags));
    case 331: return kernel::KErrorToSysReturnCode(Dispatch_SelectClipRegion_V1(data, size, flags));
    case 332: return kernel::KErrorToSysReturnCode(Dispatch_CombineRegion_V1(data, size, flags));
    case 333: return kernel::KErrorToSysReturnCode(Dispatch_SetViewportOrg_V1(data, size, flags));
    case 334: return kernel::KErrorToSysReturnCode(Dispatch_SetWindowOrg_V1(data, size, flags));
    case 340: return kernel::KErrorToSysReturnCode(Dispatch_GetTextMetrics_V1(data, size, flags));
    case 341: return kernel::KErrorToSysReturnCode(Dispatch_GetBitmapBits_V1(data, size, flags));
    case 342: return kernel::KErrorToSysReturnCode(Dispatch_SetBitmapBits_V1(data, size, flags));
    case 343: return kernel::KErrorToSysReturnCode(Dispatch_GetDeviceCaps_V1(data, size, flags));
    case 344: return kernel::KErrorToSysReturnCode(Dispatch_SetDrawColors_V1(data, size, flags));
    case 345: return kernel::KErrorToSysReturnCode(Dispatch_QueryFontEngine_V1(data, size, flags));
    case 346: return kernel::KErrorToSysReturnCode(Dispatch_BeginFontUpload_V1(data, size, flags));
    case 347: return kernel::KErrorToSysReturnCode(Dispatch_AppendFontUpload_V1(data, size, flags));
    case 348: return kernel::KErrorToSysReturnCode(Dispatch_CommitFontUpload_V1(data, size, flags));
    case 349: return kernel::KErrorToSysReturnCode(Dispatch_CloseFontResource_V1(data, size, flags));
    case 350: return kernel::KErrorToSysReturnCode(Dispatch_EnumDisplayModes_V1(data, size, flags));
    case 351: return kernel::KErrorToSysReturnCode(Dispatch_GetDisplayMode_V1(data, size, flags));
    case 352: return kernel::KErrorToSysReturnCode(Dispatch_SetDisplayMode_V1(data, size, flags));
    case 400: return kernel::KErrorToSysReturnCode(Dispatch_RegisterClass_V1(data, size, flags));
    case 401: return kernel::KErrorToSysReturnCode(Dispatch_CreateWindow_V1(data, size, flags));
    case 402: return kernel::KErrorToSysReturnCode(Dispatch_DestroyWindow_V1(data, size, flags));
    case 403: return kernel::KErrorToSysReturnCode(Dispatch_ShowWindow_V1(data, size, flags));
    case 404: return kernel::KErrorToSysReturnCode(Dispatch_MoveWindow_V1(data, size, flags));
    case 405: return kernel::KErrorToSysReturnCode(Dispatch_GetWindowRect_V1(data, size, flags));
    case 406: return kernel::KErrorToSysReturnCode(Dispatch_InvalidateRect_V1(data, size, flags));
    case 407: return kernel::KErrorToSysReturnCode(Dispatch_BeginPaint_V1(data, size, flags));
    case 408: return kernel::KErrorToSysReturnCode(Dispatch_EndPaint_V1(data, size, flags));
    case 409: return kernel::KErrorToSysReturnCode(Dispatch_GetMessage_V1(data, size, flags));
    case 410: return kernel::KErrorToSysReturnCode(Dispatch_PeekMessage_V1(data, size, flags));
    case 411: return kernel::KErrorToSysReturnCode(Dispatch_PostMessage_V1(data, size, flags));
    case 412: return kernel::KErrorToSysReturnCode(Dispatch_GetWindowLongPtr_V1(data, size, flags));
    case 413: return kernel::KErrorToSysReturnCode(Dispatch_SetWindowLongPtr_V1(data, size, flags));
    case 414: return kernel::KErrorToSysReturnCode(Dispatch_SetCapture_V1(data, size, flags));
    case 415: return kernel::KErrorToSysReturnCode(Dispatch_ReleaseCapture_V1(data, size, flags));
    case 416: return kernel::KErrorToSysReturnCode(Dispatch_GetClientRect_V1(data, size, flags));
    case 417: return kernel::KErrorToSysReturnCode(Dispatch_SetFocus_V1(data, size, flags));
    case 418: return kernel::KErrorToSysReturnCode(Dispatch_SetWindowText_V1(data, size, flags));
    case 207: return kernel::KErrorToSysReturnCode(Dispatch_QueryFpu_V1(data, size, flags));
    case 500: return kernel::KErrorToSysReturnCode(Dispatch_QueryNetworkVersion_V1(data, size, flags));
    case 100: return kernel::KErrorToSysReturnCode(Dispatch_Print_V1(data, size, flags));
    case 200: return kernel::KErrorToSysReturnCode(Dispatch_CreateProcess_V1(data, size, flags));
    case 208: return kernel::KErrorToSysReturnCode(Dispatch_CreateProcess_V2(data, size, flags));
    case 201: return kernel::KErrorToSysReturnCode(Dispatch_GetInfo_V1(data, size, flags));
    case 202: return kernel::KErrorToSysReturnCode(Dispatch_Exit_V1(data, size, flags));
    case 203: return kernel::KErrorToSysReturnCode(Dispatch_WaitForObject_V1(data, size, flags));
    case 204: return kernel::KErrorToSysReturnCode(Dispatch_WaitProcess_V1(data, size, flags));
    case 205: return kernel::KErrorToSysReturnCode(Dispatch_CreateEvent_V1(data, size, flags));
    case 206: return kernel::KErrorToSysReturnCode(Dispatch_SetEvent_V1(data, size, flags));
    case 210: return kernel::KErrorToSysReturnCode(Dispatch_AllocatePages_V1(data, size, flags));
    case 211: return kernel::KErrorToSysReturnCode(Dispatch_FreePages_V1(data, size, flags));
    case 212: return kernel::KErrorToSysReturnCode(Dispatch_Protect_V1(data, size, flags));
    case 213: return kernel::KErrorToSysReturnCode(Dispatch_MapFixedPages_V1(data, size, flags));
    case 220: return kernel::KErrorToSysReturnCode(Dispatch_Open_V1(data, size, flags));
    case 221: return kernel::KErrorToSysReturnCode(Dispatch_Close_V1(data, size, flags));
    case 222: return kernel::KErrorToSysReturnCode(Dispatch_Read_V1(data, size, flags));
    case 223: return kernel::KErrorToSysReturnCode(Dispatch_Write_V1(data, size, flags));
    case 226: return kernel::KErrorToSysReturnCode(Dispatch_Write_V2(data, size, flags));
    case 224: return kernel::KErrorToSysReturnCode(Dispatch_Control_V1(data, size, flags));
    case 225: return kernel::KErrorToSysReturnCode(Dispatch_GetStdHandle_V1(data, size, flags));
    case 250: return kernel::KErrorToSysReturnCode(Dispatch_Create_V1(data, size, flags));
    case 251: return kernel::KErrorToSysReturnCode(Dispatch_SetConfig_V1(data, size, flags));
    case 252: return kernel::KErrorToSysReturnCode(Dispatch_GetConfig_V1(data, size, flags));
  }
  return SyscallReturnCode_UNKNOWN;
}


