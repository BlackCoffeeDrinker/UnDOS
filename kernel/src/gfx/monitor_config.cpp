
#include "stdkrn.hpp"

#include "gfx/gfx.hpp"

#include <kernel/configuration_manager.hpp>

namespace {
// Parses a decimal unsigned integer. Errors: InvalidFormat (empty/malformed),
// ArithmeticOverflow (does not fit in 32 bits).
kstd::expected<uint32_t, kernel::KernelError> ParseUInt32(const kstd::string_view &text) {
  if (text.empty()) return kstd::unexpected(kernel::KernelError::InvalidFormat);
  uint32_t value = 0;
  for (const char c: text) {
    if (c < '0' || c > '9') return kstd::unexpected(kernel::KernelError::InvalidFormat);
    const auto digit = static_cast<uint32_t>(c - '0');
    if (value > (UINT32_MAX - digit) / 10) return kstd::unexpected(kernel::KernelError::ArithmeticOverflow);
    value = value * 10 + digit;
  }
  return value;
}

// Reads the named value subkey under `key`, matching the "open a subkey
// named after the value, then read its default value" pattern the registry
// uses everywhere else (see kernel/src/legacy_isa_enum.cpp).
kstd::expected<kstd::string, kernel::KernelError> ReadStringValue(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName) {
  const auto valueKey = KE_CM_OpenKey(key, valueName);
  if (!valueKey) return kstd::unexpected(valueKey.error());
  const auto size = KE_CM_GetKeyValueStringSize(*valueKey);
  if (!size) return kstd::unexpected(size.error());
  kstd::string out;
  out.resize(*size);
  if (out.size() != *size) return kstd::unexpected(kernel::KernelError::OutOfMemory);
  const auto copied = KE_CM_GetKeyValueString(*valueKey, kstd::span(out.data(), out.size()));
  if (!copied) return kstd::unexpected(copied.error());
  out.resize(*copied);
  return out;
}

// Leaves `out` untouched when the value is missing or malformed.
void ReadUInt32Value(const kernel::KConfigurationKeyPtr &key, const kstd::string_view &valueName, uint32_t &out) {
  const auto text = ReadStringValue(key, valueName);
  if (!text) return;
  if (const auto value = ParseUInt32(*text)) {
    out = *value;
  } else {
    KE_Print("GFX: default monitor value {} is malformed: {}", valueName, value.error());
  }
}

// Opens \System\<name> in the LocalMachine hive, or null if any level is
// missing.
kernel::KConfigurationKeyPtr OpenSystemKey(const kstd::string_view &name) {
  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) return nullptr;
  const auto systemKey = KE_CM_OpenKey(*localMachine, "System");
  if (!systemKey) return nullptr;
  const auto key = KE_CM_OpenKey(*systemKey, name);
  return key ? *key : nullptr;
}

// Reads the Max* limits and the Modes/N timing list out of
// \System\Monitors\Default. Returns false when the key itself is unusable;
// individual malformed entries are skipped and logged.
bool ReadDefaultMonitorKey(kernel::KMonitorCapabilities &caps) {
  const auto monitorsKey = OpenSystemKey("Monitors");
  if (!monitorsKey) {
    KE_Print("GFX: no System\\Monitors key");
    return false;
  }
  const auto opened = KE_CM_OpenKey(monitorsKey, "Default");
  if (!opened) {
    KE_Print("GFX: no System\\Monitors\\Default key");
    return false;
  }
  const kernel::KConfigurationKeyPtr &defaultKey = *opened;

  ReadUInt32Value(defaultKey, "MaxHorizontalResolution", caps.maxWidth);
  ReadUInt32Value(defaultKey, "MaxVerticalResolution", caps.maxHeight);
  ReadUInt32Value(defaultKey, "MaxRefreshRate", caps.maxRefreshRate);

  if (const auto modes = KE_CM_OpenKey(defaultKey, "Modes")) {
    const kernel::KConfigurationKeyPtr &modesKey = *modes;
    for (size_t index = 0; index < kernel::KMonitorCapabilities::MaxTimings; ++index) {
      kstd::static_string<16> valueName;
      kstd::format(valueName, "{}", index);

      const auto text = ReadStringValue(modesKey, valueName);
      if (!text) {
        break;// Contiguous list: the first gap ends it
      }

      const auto timing = kernel::gfx::parse_timing(*text);
      if (!timing) {
        KE_Print("GFX: default monitor timing {} ('{}') is malformed ({}), skipped", index, *text, timing.error());
        continue;
      }
      if (caps.Add(*timing) != kernel::KernelError::Success) {
        KE_Print("GFX: default monitor timing list is full; timing {} and later ignored", index);
        break;
      }
    }
  }

  return caps.count != 0 || caps.maxWidth != 0 || caps.maxHeight != 0;
}
}// namespace

namespace kernel::gfx {
kstd::expected<KMonitorTiming, KernelError> parse_timing(const kstd::string_view &text) {
  const auto xPos = text.find('x');
  if (xPos == kstd::string_view::npos) return kstd::unexpected(KernelError::InvalidFormat);

  const auto atPos = text.find('@');
  const auto heightEnd = atPos == kstd::string_view::npos ? text.size() : atPos;
  if (heightEnd <= xPos + 1) return kstd::unexpected(KernelError::InvalidFormat);

  const auto width = ParseUInt32(text.substr(0, xPos));
  if (!width) return kstd::unexpected(width.error());
  const auto height = ParseUInt32(text.substr(xPos + 1, heightEnd - xPos - 1));
  if (!height) return kstd::unexpected(height.error());

  uint32_t refreshRate = 0;
  if (atPos != kstd::string_view::npos) {
    const auto refresh = ParseUInt32(text.substr(atPos + 1));
    if (!refresh) return kstd::unexpected(refresh.error());
    refreshRate = *refresh;
  }

  return KMonitorTiming{.width = *width, .height = *height, .refreshRate = refreshRate};
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
    if (caps.Add(KMonitorTiming{.width = mode.width, .height = mode.height, .refreshRate = mode.refreshRate}) !=
        KernelError::Success) {
      break;
    }
    if (mode.width > caps.maxWidth) caps.maxWidth = mode.width;
    if (mode.height > caps.maxHeight) caps.maxHeight = mode.height;
    if (mode.refreshRate > caps.maxRefreshRate) caps.maxRefreshRate = mode.refreshRate;
  }

  return caps;
}

}// namespace kernel::gfx
