
#include "stdkrn.hpp"

#include "gfx/gfx.hpp"

#include <kernel/configuration_manager.hpp>

namespace {
// Parses a decimal unsigned integer; returns false on empty/malformed input.
bool ParseUInt32(const kstd::string_view &text, uint32_t &out) {
  if (text.empty()) return false;
  uint32_t value = 0;
  for (const char c: text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint32_t>(c - '0');
  }
  out = value;
  return true;
}

// Reads the named value subkey under `key`, matching the "open a subkey
// named after the value, then read its default value" pattern the registry
// uses everywhere else (see kernel/src/legacy_isa_enum.cpp).
bool ReadStringValue(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName, kstd::string &out) {
  const auto valueKey = KE_CM_OpenKey(key, valueName);
  if (!valueKey) return false;
  out.resize(KE_CM_GetKeyValueStringSize(valueKey));
  return KE_CM_GetKeyValueString(valueKey, kstd::span(out.data(), out.size()));
}

bool ReadUInt32Value(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName, uint32_t &out) {
  kstd::string text;
  if (!ReadStringValue(key, valueName, text)) return false;
  return ParseUInt32(text, out);
}

// Opens \System\<name> in the LocalMachine hive, or null if any level is
// missing.
kernel::KConfigurationKeyPtr OpenSystemKey(const kstd::string_view &name) {
  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) return nullptr;
  const auto systemKey = KE_CM_OpenKey(localMachine, "System");
  return systemKey ? KE_CM_OpenKey(systemKey, name) : nullptr;
}

// Reads the Max* limits and the Modes/N timing list out of
// \System\Monitors\Default. Returns false when the key itself is unusable;
// individual malformed entries are skipped and logged.
bool ReadDefaultMonitorKey(kernel::KMonitorCapabilities &caps) {
  const auto monitorsKey = OpenSystemKey("Monitors");
  const auto defaultKey = monitorsKey ? KE_CM_OpenKey(monitorsKey, "Default") : nullptr;
  if (!defaultKey) {
    KE_Print("GFX: no System\\Monitors\\Default key");
    return false;
  }

  ReadUInt32Value(defaultKey, "MaxHorizontalResolution", caps.maxWidth);
  ReadUInt32Value(defaultKey, "MaxVerticalResolution", caps.maxHeight);
  ReadUInt32Value(defaultKey, "MaxRefreshRate", caps.maxRefreshRate);

  if (const auto modesKey = KE_CM_OpenKey(defaultKey, "Modes")) {
    for (size_t index = 0; index < kernel::KMonitorCapabilities::MaxTimings; ++index) {
      kstd::static_string<16> valueName;
      kstd::format(valueName, "{}", index);

      kstd::string text;
      if (!ReadStringValue(modesKey, valueName, text)) {
        break;// Contiguous list: the first gap ends it
      }

      kernel::KMonitorTiming timing{};
      if (!kernel::gfx::parse_timing(text, timing)) {
        KE_Print("GFX: default monitor timing {} ('{}') is malformed, skipped", index, text);
        continue;
      }
      caps.Add(timing);
    }
  }

  return caps.count != 0 || caps.maxWidth != 0 || caps.maxHeight != 0;
}
}// namespace

namespace kernel::gfx {
bool parse_timing(const kstd::string_view &text, KMonitorTiming &out) {
  const auto xPos = text.find('x');
  if (xPos == kstd::string_view::npos) return false;

  const auto atPos = text.find('@');
  const auto heightEnd = atPos == kstd::string_view::npos ? text.size() : atPos;
  if (heightEnd <= xPos + 1) return false;

  uint32_t width = 0;
  uint32_t height = 0;
  if (!ParseUInt32(text.substr(0, xPos), width)) return false;
  if (!ParseUInt32(text.substr(xPos + 1, heightEnd - xPos - 1), height)) return false;

  uint32_t refreshRate = 0;
  if (atPos != kstd::string_view::npos && !ParseUInt32(text.substr(atPos + 1), refreshRate)) {
    return false;
  }

  out.width = width;
  out.height = height;
  out.refreshRate = refreshRate;
  return true;
}

const KMonitorCapabilities &default_monitor_capabilities() {
  // The hive walk is not cheap and the answer never changes within a boot, so
  // it is resolved once for every non-PnP head in the machine.
  static bool resolved = false;
  static KMonitorCapabilities caps{};

  if (!resolved) {
    resolved = true;

    KMonitorCapabilities fromRegistry{};
    fromRegistry.isDefault = true;
    if (ReadDefaultMonitorKey(fromRegistry)) {
      caps = fromRegistry;
      KE_Print("GFX: default monitor has {} timings (max {}x{}@{})",
               caps.count, caps.maxWidth, caps.maxHeight, caps.maxRefreshRate);
    } else {
      caps = KMonitorCapabilities::BuiltinFallback();
      KE_Print("GFX: using the built-in 640x480@60 default monitor capabilities");
    }
  }

  return caps;
}

KMonitorCapabilities capabilities_from_edid(const Edid &edid) {
  KMonitorCapabilities caps{};
  if (!edid.IsValid()) {
    return caps;
  }

  kstd::vector<KDisplayModeInfo> modes;
  edid.ExtractSupportedModes(modes);

  for (const auto &mode: modes) {
    if (mode.width == 0 || mode.height == 0) continue;
    if (!caps.Add(KMonitorTiming{.width = mode.width, .height = mode.height, .refreshRate = mode.refreshRate})) {
      break;
    }
    if (mode.width > caps.maxWidth) caps.maxWidth = mode.width;
    if (mode.height > caps.maxHeight) caps.maxHeight = mode.height;
    if (mode.refreshRate > caps.maxRefreshRate) caps.maxRefreshRate = mode.refreshRate;
  }

  return caps;
}

}// namespace kernel::gfx
