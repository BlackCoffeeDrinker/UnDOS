
#pragma once

#include <array.hpp>
#include <static_string.hpp>

namespace kernel {
static constexpr size_t MaxEdidSize = 512;

struct KGraphicsAdapterDisplayInfo {
  bool isPresent{false};// Whether the display is present
  bool isMonitor{false};// Whether the display is a monitor (or integrated sub-device on the graphics card (e.g., secondary GPU head or hardware video decoder))
  bool isPnP{false};    // Can the graphics adapter enumerate the display (e.g., via EDID)

  kstd::array<uint8_t, MaxEdidSize> edid{};// Raw EDID data
  size_t edidSize{0};

  kstd::static_string<64> name{};
};

}// namespace kernel
