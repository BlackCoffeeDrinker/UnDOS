
#pragma once

#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/gfx/edid.hpp>
#include <kernel/gfx/monitor.hpp>

namespace kernel {
struct KGraphicsAdapter;
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

// Reads the preferred desktop mode from \System\Console\Desktop. Returns
// false when the key is absent or its Width/Height/Bpp are unparsable.
bool read_desktop_preference(uint32_t &width, uint32_t &height, uint32_t &bpp, uint32_t &refreshRate);

// Parses a "<width>x<height>@<refresh>" timing string; the "@<refresh>"
// suffix is optional and yields a refreshRate of 0 ("any").
bool parse_timing(const kstd::string_view &text, KMonitorTiming &out);
}// namespace kernel::gfx
