#include "gfx/process_state.hpp"


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

kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> CreateDC(KProcessObject *process) {
  // Checked under gfx_lock() so the exit sweep either sees this DC or it is never created.
  if (process->state != KProcessObject::State::Running) {
    return kstd::unexpected(make_error_code(KernelError::InvalidState));
  }
  return AllocateDC(key_of(*process));
}

// The process parameter is kept for a future access check; it is ignored for now.
KDeviceContextSlot *FindDC(KProcessObject *, HDC HDC) { return ResolveDC(HDC); }

kstd::error_code CloseDC(KProcessObject *, HDC HDC, DetachedDC &released) { return CloseDCSlot(HDC, released); }

kstd::error_code CloseDC(KProcessObject *, HDC HDC) {
  DetachedDC released;
  return CloseDCSlot(HDC, released);
}

font::ResourceTable &font_resources() noexcept { return g_font_resources; }

}// namespace kernel::gfx
