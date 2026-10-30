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

// Where the pixels of a prepared target mode live. The backing is reserved
// before hardware programming begins.
struct KDisplaySurfaceBinding {
  KObjectPtr<KMemoryBackingObject> backing{nullptr};// Memory holding the pixel data
  SurfaceType type{SurfaceType::Framebuffer};       // VRAM scanout vs shadow in RAM
  uint32_t flags{0};                                // Bitfield of SurfaceFlags
};

struct KDisplayModeReservation {
  size_t modeIndex{};
  KDisplayModeInfo mode{};
  KDisplaySurfaceBinding binding{};
  void *driverData{};
};

struct KDisplayActualState {
  bool known{false};
  bool enabled{false};
  size_t modeIndex{};
};

// Physical video-present target and its current scanout surface.
struct KVideoPresentTarget : KObjectT<KVideoPresentTarget, 1, TYPE_VIDEO_PRESENT_TARGET> {
  using KObjectT::KObjectT;

  // Parent Adapter & Hardware Head Mapping
  KObjectPtr<KGraphicsAdapter> adapter{nullptr};
  uint32_t headIndex{0};// Controller head / DAC index on adapter
  uint32_t videoPresentTargetId{0};

  // Hardware Identification & Hotplug Status
  bool isPresent{false};          // Connector attached physically
  bool isMonitor{false};          // Valid monitor/sink detected (DDC or load check)
  bool isPrimary{false};          // Designated primary display output
  bool isAttachedToDesktop{false};// Currently active in global virtual desktop
  bool topologyDegraded{false};
  bool removed{false};

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

  // Target-mode enumeration and transaction callbacks. Validate checks that
  // the target is still usable; PrepareMode reserves resources without
  // changing hardware. FinishMode releases a failed or abandoned reservation
  // or transfers its ownership after a successful program.
  // Probe mode ordinals until OutOfRange. IDs exposed to callers are manager-owned.
  cfunc<kstd::expected<KDisplayModeInfo, kstd::error_code>(const KObjectPtr<KDisplay> &display, size_t modeIndex)> GetModeInfo;

  // Prepare allocates every driver-owned resource needed by ProgramMode
  // without changing hardware. FinishMode releases a failed reservation or
  // transfers successful driverData ownership to the target context.
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display, size_t modeIndex,
                         KDisplayModeReservation &reservation)> PrepareMode;
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display,
                         KDisplayModeReservation &reservation)> ProgramMode;
  cfunc<void(const KObjectPtr<KDisplay> &display,
             KDisplayModeReservation &reservation, bool committed)> FinishMode;

  // RestoreMode is optional; QueryActualState must describe only known state.
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display, size_t modeIndex,
                         KDisplayModeReservation &reservation)> RestoreMode;
  cfunc<kstd::expected<KDisplayActualState, kstd::error_code>(const KObjectPtr<KDisplay> &display)> QueryActualState;
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display)> Enable;  // Enable output timings & DAC
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display)> Disable;// Blank CRTC and disable signal output
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display)> Validate;// Re-verify DDC link and connection status

  // Power & DPMS Control Hooks
  cfunc<kstd::error_code(const KObjectPtr<KDisplay> &display, DisplayPowerState state)> SetPowerState;
};

template<>
struct ObjectTypeOf<TYPE_VIDEO_PRESENT_TARGET> {
  using type = KVideoPresentTarget;
};
}// namespace kernel
