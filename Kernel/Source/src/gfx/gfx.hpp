
#pragma once

#include <expected.hpp>

#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/fwd/KDisplay.hpp>
#include <kernel/gfx/edid.hpp>
#include <kernel/gfx/monitor.hpp>
#include <kernel/gfx/video_present.hpp>
#include <kernel/kerror.hpp>

namespace kernel {
struct KGraphicsAdapter;
}

namespace kernel::gfx {
void wait_for_drivers() noexcept;

// Runs the probe/configure loop for an adapter whose backing device has
// started: this is the sole creator of KDisplay objects. Normally invoked from
// the PnP device event; exposed here so the hosted tests can drive it.
void start_adapter(const KObjectPtr<KGraphicsAdapter> &adapter) noexcept;

// PnP removal entry: drains USER drawing, retires matching targets and marks
// the retained device stack Removed under scene -> drawing -> topology order.
// Call before hardware/resource teardown, outside USER and topology locks.
void remove_device(const KDevicePtr<KDevice> &base) noexcept;

// Capabilities assumed for a head with no usable EDID, read from
// \System\Monitors\Default. Cached after the first call; degrades to
// KMonitorCapabilities::BuiltinFallback() when the key is unusable.
const KMonitorCapabilities &default_monitor_capabilities();

// Derives the capabilities of a PnP head from its EDID timing list.
KMonitorCapabilities capabilities_from_edid(const Edid &edid);

// Monitor-approved modes whose geometry USER can represent.
bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode) noexcept;
bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode,
                 const KMonitorCapabilities &capabilities) noexcept;
// Returns the `ordinal`th usable mode (modeIndex filled in). Errors:
// InvalidArgument (null display), Unsupported (no GetModeInfo hook),
// OutOfRange (fewer usable modes).
// NotFound marks the end of supported-mode enumeration.
kstd::expected<KDisplayModeInfo, kstd::error_code> enumerate_mode(const KObjectPtr<KDisplay> &display, uint32_t ordinal) noexcept;
KObjectPtr<KDisplay> active_video_present_target() noexcept;
bool has_video_present_targets() noexcept;

// Parses a "<width>x<height>@<refresh>" timing string; the "@<refresh>"
// suffix is optional and yields a refreshRate of 0 ("any"). Errors:
// InvalidFormat (malformed text), ArithmeticOverflow (number too large).
kstd::expected<KMonitorTiming, kstd::error_code> parse_timing(const kstd::string_view &text);
}// namespace kernel::gfx
