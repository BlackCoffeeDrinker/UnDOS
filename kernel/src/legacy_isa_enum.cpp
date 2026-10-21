#include "legacy_isa_enum.hpp"

#include "pnp_manager.hpp"
#include "stdkrn.hpp"

#include <Kernel.hpp>
#include <kernel/configuration_manager.hpp>
#include <kernel/driver.hpp>
#include <kernel/resource.hpp>

namespace {

// Per-instance data stashed on the reported PDO's own deviceExtension, since
// QueryResourceRequirements is only ever handed the PDO itself (see
// pnp_manager.cpp's ProcessDevice): one shared callback below reconstructs
// each device's IoResourceRequirementsList straight from these fields.
struct LegacyReservationContext {
  bool hasPort = false;
  uint32_t portStart = 0;
  uint32_t portLength = 0;
  bool hasMemory = false;
  uint32_t memoryStart = 0;
  uint32_t memoryLength = 0;
  bool hasIrq = false;
  uint32_t irq = 0;
  bool hasDma = false;
  uint32_t dma = 0;
};

// Parses a decimal ("5") or 0x-prefixed hex ("0x220") unsigned integer.
// Leaves out untouched on empty, malformed or overflowing input.
kstd::error_code ParseUInt32(const kstd::string_view &text, uint32_t &out) {
  if (text.empty()) return kernel::KernelError::InvalidFormat;

  size_t i = 0;
  uint32_t base = 10;
  if (text.length() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    i = 2;
  }
  if (i >= text.length()) return kernel::KernelError::InvalidFormat;

  uint32_t value = 0;
  for (; i < text.length(); ++i) {
    const char c = text[i];
    uint32_t digit;
    if (c >= '0' && c <= '9') {
      digit = static_cast<uint32_t>(c - '0');
    } else if (base == 16 && c >= 'a' && c <= 'f') {
      digit = static_cast<uint32_t>(c - 'a' + 10);
    } else if (base == 16 && c >= 'A' && c <= 'F') {
      digit = static_cast<uint32_t>(c - 'A' + 10);
    } else {
      return kernel::KernelError::InvalidFormat;
    }
    if (value > (UINT32_MAX - digit) / base) return kernel::KernelError::ArithmeticOverflow;
    value = value * base + digit;
  }

  out = value;
  return {};
}

// Parses a "start-end" range (each side per ParseUInt32) as found in the
// registry's IoPorts/Memory values, e.g. "0x3C0-0x3DF".
kstd::error_code ParseRange(const kstd::string_view &text, uint32_t &start, uint32_t &end) {
  const auto dash = text.find('-');
  if (dash == kstd::string_view::npos) return kernel::KernelError::InvalidFormat;
  if (const auto error = ParseUInt32(text.substr(0, dash), start);
      error) return error;
  if (const auto error = ParseUInt32(text.substr(dash + 1), end);
      error) return error;
  return end >= start ? kernel::KernelError::Success : kernel::KernelError::OutOfRange;
}

// Reads the named value subkey (e.g. "IoPorts") under deviceKey, matching the
// "open a subkey named after the value, then read its default value" pattern
// LoadDriversWithStart() already uses for "Start"/"ImagePath". Sizes `out`
// via KE_CM_GetKeyValueStringSize() and has KE_CM_GetKeyValueString() write
// straight into it, rather than aliasing any scratch storage owned by
// valueKey.
kstd::error_code ReadStringValue(const kernel::KConfigurationKeyPtr &deviceKey, const kstd::string_view &valueName, kstd::string &out) {
  const auto valueKey = KE_CM_OpenKey(deviceKey, valueName);
  if (!valueKey) return valueKey.error();
  const auto size = KE_CM_GetKeyValueStringSize(*valueKey);
  if (!size) return size.error();
  out.resize(*size);
  if (out.size() != *size) return kernel::KernelError::OutOfMemory;
  const auto copied = KE_CM_GetKeyValueString(*valueKey, kstd::span(out.data(), out.size()));
  if (!copied) return copied.error();
  return {};
}

// Best-effort generic ISA presence probe: reads back the device's first
// configured I/O port and treats a floating/absent bus (0xFF) as "not
// present" - the same sanity check bus_isa uses for its legacy ATA channels
// (channel.io + 7) and the old VGA-specific enumerator used for 0x3CC.
// Devices with no I/O ports at all (a fixed memory-only window) are assumed
// present, since there is nothing to sample.
bool ProbeLegacyDevice(const LegacyReservationContext &ctx) {
  if (!ctx.hasPort) return true;
  return HAL_IO_In8(static_cast<uint16_t>(ctx.portStart)) != 0xFF;
}

// Returned to the PnP manager against the enumerator's own ephemeral
// KDriverObject (before AddDevice rebinds pdo->driverObject to the matched
// function driver), once per reported PDO. A fresh list is built each call
// since ProcessDevice frees it after assigning resources.
kstd::expected<kernel::IoResourceRequirementsList *, kstd::error_code>
LegacyIsaEnum_QueryResourceRequirements(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto *ctx = pdo->deviceExtension.as<LegacyReservationContext>();
  if (!ctx) return kstd::unexpected(kernel::KernelError::InvalidState);

  const auto created = KE_RES_CreateRequirementsList(kernel::device_type::Unknown);
  if (!created) return kstd::unexpected(created.error());
  auto *req = *created;

  const auto added = KE_RES_AddAlternative(req);
  if (!added) {
    KE_RES_FreeRequirementsList(req);
    return kstd::unexpected(added.error());
  }
  auto *alt = *added;

  kstd::error_code error = kernel::KernelError::Success;
  if (ctx->hasPort) {
    error = KE_RES_AddPort(alt, ctx->portStart, ctx->portLength, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (!error && ctx->hasMemory) {
    error = KE_RES_AddMemory(alt, ctx->memoryStart, ctx->memoryLength, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (!error && ctx->hasIrq) {
    error = KE_RES_AddInterrupt(alt, ctx->irq, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (!error && ctx->hasDma) {
    error = KE_RES_AddDma(alt, ctx->dma, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (error) {
    KE_RES_FreeRequirementsList(req);
    return kstd::unexpected(error);
  }
  return req;
}

// Reads one LegacyISAReservations\<childName> subkey. HwID is the only
// mandatory value (it becomes both the ProbeDevice dispatch key and the
// reported PDO's hardwareId); IoPorts/Memory/Irq/Dma/DriverImage are all
// optional, matching the fact that not every legacy device needs every kind
// of resource (e.g. a speaker has neither ports nor an image to load).
kstd::error_code LoadLegacyReservation(
    const kernel::KConfigurationKeyPtr &deviceKey,
    const kstd::string_view &childName,
    kstd::string &outHwId,
    kstd::string &outDriverImage,
    LegacyReservationContext &ctx) {
  if (const auto error = ReadStringValue(deviceKey, "HwID", outHwId);
      error || outHwId.empty()) {
    KE_Print("LegacyIsaEnum: '{}' has no HwID, skipping", childName);
    return error ? error : kernel::KernelError::InvalidArgument;
  }

  // DriverImage is optional: if absent, a driver already registered under
  // this HwID (e.g. loaded earlier as a Boot/Automatic driver) is expected
  // to bind instead.
  if (const auto error = ReadStringValue(deviceKey, "DriverImage", outDriverImage);
      error && error != kernel::KernelError::NotFound) return error;

  kstd::string ioPorts;
  if (const auto error = ReadStringValue(deviceKey, "IoPorts", ioPorts);
      !error) {
    uint32_t start = 0, end = 0;
    if (const auto parsed = ParseRange(ioPorts, start, end); !parsed) {
      if (end - start == UINT32_MAX) return kernel::KernelError::ArithmeticOverflow;
      ctx.hasPort = true;
      ctx.portStart = start;
      ctx.portLength = end - start + 1;
    } else {
      KE_Print("LegacyIsaEnum: '{}' has invalid IoPorts '{}'", childName, ioPorts);
      return parsed;
    }
  } else if (error != kernel::KernelError::NotFound) return error;

  kstd::string memory;
  if (const auto error = ReadStringValue(deviceKey, "Memory", memory);
      !error) {
    uint32_t start = 0, end = 0;
    if (const auto parsed = ParseRange(memory, start, end); !parsed) {
      if (end - start == UINT32_MAX) return kernel::KernelError::ArithmeticOverflow;
      ctx.hasMemory = true;
      ctx.memoryStart = start;
      ctx.memoryLength = end - start + 1;
    } else {
      KE_Print("LegacyIsaEnum: '{}' has invalid Memory '{}'", childName, memory);
      return parsed;
    }
  } else if (error != kernel::KernelError::NotFound) return error;

  kstd::string irq;
  if (const auto error = ReadStringValue(deviceKey, "Irq", irq);
      !error) {
    if (const auto parsed = ParseUInt32(irq, ctx.irq); !parsed) {
      ctx.hasIrq = true;
    } else {
      KE_Print("LegacyIsaEnum: '{}' has invalid Irq '{}'", childName, irq);
      return parsed;
    }
  } else if (error != kernel::KernelError::NotFound) return error;

  kstd::string dma;
  if (const auto error = ReadStringValue(deviceKey, "Dma", dma);
      !error) {
    if (const auto parsed = ParseUInt32(dma, ctx.dma); !parsed) {
      ctx.hasDma = true;
    } else {
      KE_Print("LegacyIsaEnum: '{}' has invalid Dma '{}'", childName, dma);
      return parsed;
    }
  } else if (error != kernel::KernelError::NotFound) return error;

  return {};
}

// True when the entry opts out of the normal enumeration pass by declaring
// Start=LastResort, mirroring the driver-level Start value: such an entry is
// only reported if nothing else claimed the hardware it stands in for.
kstd::expected<bool, kstd::error_code> IsLastResortEntry(const kernel::KConfigurationKeyPtr &deviceKey) {
  kstd::string start;
  if (const auto error = ReadStringValue(deviceKey, "Start", start); error) {
    if (error == kernel::KernelError::NotFound) return false;
    return kstd::unexpected(error);
  }
  return start == "LastResort";
}

// One enumeration pass. `lastResort` selects which half of the
// LegacyISAReservations entries is considered, so the two passes together
// cover every entry exactly once.
void EnumerateLegacyEntries(bool lastResort) {
  const auto openedRoot = KE_CM_OpenRoot("LocalMachine");
  if (!openedRoot) {
    KE_Print("LegacyIsaEnum: failed to open LocalMachine: {}", openedRoot.error().message());
    return;
  }
  const auto &localMachine = *openedRoot;

  const auto openedLegacy = KE_CM_OpenKey(localMachine, "LegacyISAReservations");
  if (!openedLegacy) {
    KE_Print("LegacyIsaEnum: cannot open LegacyISAReservations: {}", openedLegacy.error().message());
    return;
  }
  const auto &legacyKey = *openedLegacy;

  for (uint32_t index = 0;; ++index) {
    const auto child = KE_CM_EnumKey(legacyKey, index);
    if (!child) {
      KE_Print("LegacyIsaEnum: failed to enumerate reservations: {}", child.error().message());
      break;
    }
    if (!child->available) {
      break;
    }
    const auto childName = child->name;

    const auto openedDevice = KE_CM_OpenKey(legacyKey, childName);
    if (!openedDevice) {
      KE_Print("LegacyIsaEnum: failed to open '{}': {}", childName, openedDevice.error().message());
      continue;
    }
    const auto &deviceKey = *openedDevice;

    const auto lastResortEntry = IsLastResortEntry(deviceKey);
    if (!lastResortEntry) {
      KE_Print("LegacyIsaEnum: failed to read Start for '{}': {}", childName,
               lastResortEntry.error().message());
      continue;
    }
    if (*lastResortEntry != lastResort) {
      continue;
    }

    kstd::string hwid;
    kstd::string driverImage;
    LegacyReservationContext ctx;
    if (const auto error = LoadLegacyReservation(deviceKey, childName, hwid, driverImage, ctx);
        error) {
      KE_Print("LegacyIsaEnum: failed to load reservation '{}': {}", childName,
               error.message());
      continue;
    }

    if (!ProbeLegacyDevice(ctx)) {
      KE_Print("LegacyIsaEnum: '{}' (HwID={}) not detected", childName, hwid);
      continue;
    }

    // Load the device's own driver image (if any) up front, so it is
    // registered by the time the PnP worker thread matches the PDO's
    // hardware id.
    if (!driverImage.empty()) {
      if (const auto driver = KE_DRIVER_Load(driverImage); !driver) {
        KE_Print("LegacyIsaEnum: failed to load driver '{}': {}", driverImage,
                 driver.error().message());
        continue;
      }
    }

    const auto createdEnumerator = kernel::CreateKObject<kernel::KDriverObject>("LegacyIsaEnum");
    if (!createdEnumerator) {
      KE_Print("LegacyIsaEnum: failed to create enumerator driver object for '{}'", childName);
      continue;
    }
    const auto &enumerator = *createdEnumerator;
    enumerator->QueryResourceRequirements = LegacyIsaEnum_QueryResourceRequirements;

    auto createdPdo = KE_IO_CreateDeviceWithContext<LegacyReservationContext>(enumerator, {}, kernel::device_type::Unknown);
    if (!createdPdo) {
      KE_Print("LegacyIsaEnum: failed to create PDO for '{}'", childName);
      continue;
    }
    const auto &pdo = *createdPdo;
    *pdo->deviceExtension.template as<LegacyReservationContext>() = ctx;
    pdo->hardwareId = hwid;

    KE_Print("LegacyIsaEnum: '{}' (HwID={}) detected, reporting PDO", childName, hwid);
    KE_PNP_ReportNewDevice(KE_PNP_GetRootDevice(), pdo);
  }
}

}// namespace

namespace kernel::driver {
void EnumerateLegacyISADevices() {
  EnumerateLegacyEntries(false);
}

void EnumerateLastResortLegacyISADevices() {
  EnumerateLegacyEntries(true);
}
}// namespace kernel::driver
