
#pragma once

#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/gfx/edid.hpp>
#include <kernel/gfx/monitor.hpp>

namespace kernel {
struct KGraphicsAdapter;
struct KDisplay;
}

namespace kernel::gfx {
void wait_for_drivers() noexcept;

// Runs the probe/configure loop for an adapter whose backing device has
// started: this is the sole creator of KDisplay objects. Normally invoked from
// the PnP device event; exposed here so the hosted tests can drive it.
void start_adapter(const KObjectPtr<KGraphicsAdapter> &adapter) noexcept;

// Capabilities assumed for a head with no usable EDID, read from
// \System\Monitors\Default. Cached after the first call; degrades to
// KMonitorCapabilities::BuiltinFallback() when the key is unusable.
const KMonitorCapabilities &default_monitor_capabilities();

// Derives the capabilities of a PnP head from its EDID timing list.
KMonitorCapabilities capabilities_from_edid(const Edid &edid);

// Monitor-approved modes whose geometry USER can represent.
bool usable_mode(const KObjectPtr<KDisplay> &display, const KDisplayModeInfo &mode) noexcept;
bool enumerate_mode(const KObjectPtr<KDisplay> &display, uint32_t ordinal, KDisplayModeInfo &out) noexcept;

// Parses a "<width>x<height>@<refresh>" timing string; the "@<refresh>"
// suffix is optional and yields a refreshRate of 0 ("any").
bool parse_timing(const kstd::string_view &text, KMonitorTiming &out);
}// namespace kernel::gfx
