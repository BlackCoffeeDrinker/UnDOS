#pragma once

#include <expected.hpp>

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/gfx/edid.hpp>
#include <kernel/gfx/monitor.hpp>
#include <kernel/kerror.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KSurface.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KGraphicsAdapter;

enum class DisplayPowerState : uint8_t {
  PowerOn = 0,
  Standby = 1,// DPMS Standby (H-Sync off)
  Suspend = 2,// DPMS Suspend (V-Sync off)
  PowerOff = 3// DPMS Power Off (H-Sync and V-Sync off)
};

enum class DisplayOrientation : uint8_t {
  Default = 0,  // 0 degrees
  Rotate90 = 1, // 90 degrees clockwise
  Rotate180 = 2,// 180 degrees
  Rotate270 = 3 // 270 degrees clockwise
};

// Where the pixels of a newly programmed mode live. Filled in by the driver's
// KDisplay::SetMode; the GFX subsystem copies it onto the display's KSurface.
struct KDisplaySurfaceBinding {
  KObjectPtr<KMemoryBackingObject> backing{nullptr};// Memory holding the pixel data
  SurfaceType type{SurfaceType::Framebuffer};       // VRAM scanout vs shadow in RAM
  uint32_t flags{0};                                // Bitfield of SurfaceFlags
};

// Display object representing a mode/connector/framebuffer
struct KDisplay : KObjectT<KDisplay, 1, TYPE_DISPLAY> {
  using KObjectT::KObjectT;

  // Parent Adapter & Hardware Head Mapping
  KObjectPtr<KGraphicsAdapter> adapter{nullptr};
  uint32_t headIndex{0};// Controller head / DAC index on adapter

  // Hardware Identification & Hotplug Status
  bool isPresent{false};          // Connector attached physically
  bool isMonitor{false};          // Valid monitor/sink detected (DDC or load check)
  bool isPrimary{false};          // Designated primary display output
  bool isAttachedToDesktop{false};// Currently active in global virtual desktop

  Edid edid{};

  // What the attached monitor can display: derived from the EDID when the head
  // is PnP, otherwise the registry-backed default monitor. Modes the
  // capabilities reject are neither enumerated nor programmable.
  KMonitorCapabilities monitorCaps{};

  // Spatial Multi-Monitor Topology
  int32_t desktopX{0};// Horizontal origin in virtual screen coordinates
  int32_t desktopY{0};// Vertical origin in virtual screen coordinates
  DisplayOrientation orientation{DisplayOrientation::Default};

  // Framebuffer Surface & Active Hardware Mode
  KObjectPtr<KSurface> surface{nullptr};// Active scanout framebuffer surface
  KDisplayModeInfo currentMode{};       // Currently programmed mode hardware timings
  uint32_t modeGeneration{0};
  DisplayPowerState powerState{DisplayPowerState::PowerOn};

  // Driver Execution Entry Points
  // Probes mode info; modeIndex++ until this fails with KernelError::OutOfRange.
  cfunc<kstd::expected<KDisplayModeInfo, KernelError>(const KObjectPtr<KDisplay> &display, size_t modeIndex)> GetModeInfo;

  // Programs `modeIndex` in hardware and reports where the pixels live. The
  // driver does not touch KSurface; the kernel owns that.
  cfunc<kstd::expected<KDisplaySurfaceBinding, KernelError>(const KObjectPtr<KDisplay> &display, size_t modeIndex)> SetMode;
  cfunc<KernelError(const KObjectPtr<KDisplay> &display)> Enable;  // Enable output timings & DAC
  cfunc<void(const KObjectPtr<KDisplay> &display)> Disable;        // Blank CRTC and disable signal output
  cfunc<KernelError(const KObjectPtr<KDisplay> &display)> Validate;// Re-verify DDC link and connection status

  // Power & DPMS Control Hooks
  cfunc<KernelError(const KObjectPtr<KDisplay> &display, DisplayPowerState state)> SetPowerState;
};

template<>
struct ObjectTypeOf<TYPE_DISPLAY> {
  using type = KDisplay;
};
}// namespace kernel
