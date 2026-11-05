#pragma once

#include <kernel/KObject.hpp>

// Checked calls to the Step 13 adapter surface ops. No op runs on a
// Terminated adapter or on a lost surface.
namespace kernel::gfx {
inline bool adapter_usable(const KObjectPtr<KGraphicsAdapter> &adapter) noexcept {
  return adapter && adapter->state != KGraphicsAdapter::State::Terminated;
}

inline kstd::error_code create_adapter_surface(const KObjectPtr<KGraphicsAdapter> &adapter, uint32_t width,
                                               uint32_t height, PixelFormat format,
                                               KObjectPtr<KSurface> &out) noexcept {
  if (!adapter_usable(adapter) || !adapter->CreateSurface) return KernelError::Unsupported;
  if (const auto error = adapter->CreateSurface(adapter, width, height, format, out)) return error;
  if (!out) return KernelError::InvalidState;
  out->adapter = adapter;
  return {};
}

// Safe on a Terminated adapter: memory is only returned while the adapter is live.
inline void destroy_adapter_surface(KSurface &surface) noexcept {
  const auto &adapter = surface.adapter;
  if (adapter_usable(adapter) && adapter->DestroySurface) adapter->DestroySurface(adapter, &surface);
}

inline kstd::error_code map_adapter_surface(KSurface &surface, void *&pixels, uint32_t &pitch) noexcept {
  const auto &adapter = surface.adapter;
  if (!adapter_usable(adapter) || !adapter->Map) return KernelError::Unsupported;
  if (surface.flags & SurfaceFlags::Lost) return KernelError::InvalidState;
  return adapter->Map(adapter, &surface, pixels, pitch);
}
}// namespace kernel::gfx
