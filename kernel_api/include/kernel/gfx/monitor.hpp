
#pragma once

#include <array.hpp>

#include <kernel/kerror.hpp>
#include <kernel/kobject/KSurface.hpp>

namespace kernel {
// A single timing a monitor claims it can display.
struct KMonitorTiming {
  uint32_t width{0};
  uint32_t height{0};
  uint32_t refreshRate{0};// 0 means "any refresh rate at this resolution"
};

// What a monitor is known (or assumed) to be able to display. This is the
// Win2k monitor.inf analogue: the driver stays the authority on what the
// hardware can emit, these capabilities decide what the graphics subsystem
// actually exposes and accepts.
struct KMonitorCapabilities {
  static constexpr size_t MaxTimings = 32;

  kstd::array<KMonitorTiming, MaxTimings> timings{};
  size_t count{0};

  // Upper bounds; 0 means unconstrained.
  uint32_t maxWidth{0};
  uint32_t maxHeight{0};
  uint32_t maxRefreshRate{0};

  bool isDefault{false};// Assumed defaults (registry/built-in), not read from EDID

  KernelError Add(const KMonitorTiming &timing) {
    if (count >= MaxTimings) return KernelError::ResourceLimitExceeded;
    timings[count++] = timing;
    return KernelError::Success;
  }

  [[nodiscard]] bool Supports(const KDisplayModeInfo &mode) const {
    if (maxWidth != 0 && mode.width > maxWidth) return false;
    if (maxHeight != 0 && mode.height > maxHeight) return false;
    if (maxRefreshRate != 0 && mode.refreshRate > maxRefreshRate) return false;

    // No timing list at all: only the Max* limits constrain the mode.
    if (count == 0) return true;

    for (size_t i = 0; i < count; ++i) {
      const auto &timing = timings[i];
      if (timing.width != mode.width || timing.height != mode.height) continue;
      if (timing.refreshRate == 0 || mode.refreshRate == 0) return true;
      if (timing.refreshRate == mode.refreshRate) return true;
    }

    return false;
  }

  // Last-resort capability set, used when no better source is available.
  static KMonitorCapabilities BuiltinFallback() {
    KMonitorCapabilities caps{};
    caps.isDefault = true;
    caps.maxWidth = 640;
    caps.maxHeight = 480;
    caps.maxRefreshRate = 60;
    (void) caps.Add(KMonitorTiming{.width = 640, .height = 480, .refreshRate = 60});
    return caps;
  }
};
}// namespace kernel
