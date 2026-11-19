#include "gfx/process_state.hpp"

#include "gfx/screen_internal.hpp"

#include <kernel/kobject/KSurface.hpp>

namespace kernel::gfx {
namespace {
// System-wide font resources; only uncommitted uploads are released at process exit.
font::ResourceTable g_font_resources;
}// namespace


kstd::error_code on_process_notify(ProcessEvent event, KProcessObject &process, void *) noexcept {
  if (event == ProcessEvent::Exiting) {
    const auto key = key_of(process);
    // Uploads never committed die with their process; committed fonts are system-wide.
    (void) g_font_resources.release_uncommitted(key.pid, key.incarnation);
    // State is already Exiting, so CreateDC can no longer add DCs for this owner.
    (void) CloseAllOwnedBy(key);
  }

  return {};
}

kstd::error_code init() noexcept {
  return KE_PS_RegisterNotify(&on_process_notify, nullptr, ProcessNotifyOrder::Gfx);
}

kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> create_dc_locked(KProcessObject *process) {
  // Checked under gfx_lock() so the exit sweep either sees this DC or it is never created.
  if (process->state != KProcessObject::State::Running) {
    return kstd::unexpected(make_error_code(KernelError::InvalidState));
  }
  return AllocateDC(key_of(*process));
}

// The process parameter is kept for a future access check; it is ignored for now.
KDeviceContextSlot *find_dc_locked(KProcessObject *, HDC hdc) { return ResolveDC(hdc); }

kstd::error_code close_dc_locked(KProcessObject *, HDC hdc, DetachedDC &released) { return CloseDCSlot(hdc, released); }

kstd::expected<HDC, kstd::error_code> CreateDC(KProcessObject *process) {
  SceneGuard scene;
  const auto allocated = create_dc_locked(process);
  if (!allocated) return kstd::unexpected(allocated.error());
  return allocated->second;
}

bool DCExists(KProcessObject *process, HDC hdc) {
  SceneGuard scene;
  return find_dc_locked(process, hdc) != nullptr;
}

kstd::error_code CloseDC(KProcessObject *process, HDC hdc, DetachedDC &released) {
  SceneGuard scene;
  return close_dc_locked(process, hdc, released);
}

kstd::error_code CloseDC(KProcessObject *process, HDC hdc) {
  DetachedDC released;// dropped after the scene lock
  SceneGuard scene;
  return close_dc_locked(process, hdc, released);
}

kstd::error_code BindWindowDC(KProcessObject *process, HDC hdc, const WindowDCBinding &binding) {
  SceneGuard scene;
  auto *slot = find_dc_locked(process, hdc);
  if (!slot) return KernelError::InvalidHandle;
  if (binding.clip.size() > binding.clipStorage.size()) return KernelError::InvalidArgument;
  (void) slot->ChangeRenderState();
  if (binding.target) {
    slot->target = KObjectPtr<KSurface>(binding.target);
    slot->noAccel = true;
  }
  uint32_t clipCount = 0;
  size_t index = 0;
  for (const KClipRect &rect: binding.clip) {
    if (binding.clip.data() != binding.clipStorage.data()) binding.clipStorage[index] = rect;
    ++index;
    ++clipCount;
  }
  slot->originX = binding.originX;
  slot->originY = binding.originY;
  slot->clipRects = binding.clipStorage.data();
  slot->clipCount = clipCount;
  slot->clipped = true;
  slot->hwnd = binding.hwnd;
  return {};
}

void DetachWindowDC(KProcessObject *process, HDC hdc) {
  SceneGuard scene;
  auto *slot = find_dc_locked(process, hdc);
  if (!slot) return;
  (void) slot->ChangeRenderState();
  slot->clipRects = nullptr;
  slot->clipCount = 0;
  slot->clipped = true;
  // USER no longer tracks the DC; the application may delete it.
  slot->hwnd = 0;
}

kstd::error_code CloseWindowDC(KProcessObject *process, HDC hdc, KClipRect &damage, bool &incomplete,
                               DetachedDC &released) {
  damage = {};
  incomplete = false;
  SceneGuard scene;
  const auto *slot = find_dc_locked(process, hdc);
  if (!slot) return KernelError::InvalidHandle;
  incomplete = slot->incompleteDraw;
  if (slot->presentationDirty)
    damage = slot->presentationBounds;
  return close_dc_locked(process, hdc, released);
}

void ResetDCPresentation(KProcessObject *process, HDC hdc) {
  SceneGuard scene;
  if (auto *slot = find_dc_locked(process, hdc)) {
    slot->presentationDirty = false;
    slot->presentationBounds = {};
  }
}

font::ResourceTable &font_resources() noexcept { return g_font_resources; }

uint32_t FontRegistryHandle(uint32_t face) { return g_font_resources.registry_handle(face); }

}// namespace kernel::gfx
