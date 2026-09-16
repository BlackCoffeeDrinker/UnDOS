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
// Returns false (leaving out untouched) on empty or malformed input.
bool ParseUInt32(const kstd::string_view &text, uint32_t &out) {
  if (text.empty()) return false;

  size_t i = 0;
  uint32_t base = 10;
  if (text.length() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    i = 2;
  }
  if (i >= text.length()) return false;

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
      return false;
    }
    value = value * base + digit;
  }

  out = value;
  return true;
}

// Parses a "start-end" range (each side per ParseUInt32) as found in the
// registry's IoPorts/Memory values, e.g. "0x3C0-0x3DF".
bool ParseRange(const kstd::string_view &text, uint32_t &start, uint32_t &end) {
  const auto dash = text.find('-');
  if (dash == kstd::string_view::npos) return false;
  return ParseUInt32(text.substr(0, dash), start) && ParseUInt32(text.substr(dash + 1), end);
}

// Reads the named value subkey (e.g. "IoPorts") under deviceKey, matching the
// "open a subkey named after the value, then read its default value" pattern
// LoadDriversWithStart() already uses for "Start"/"ImagePath". Sizes `out`
// via KE_CM_GetKeyValueStringSize() and has KE_CM_GetKeyValueString() write
// straight into it, rather than aliasing any scratch storage owned by
// valueKey.
bool ReadStringValue(const kernel::KConfigurationKeyPtr &deviceKey, const kstd::string_view &valueName, kstd::string &out) {
  const auto valueKey = KE_CM_OpenKey(deviceKey, valueName);
  if (!valueKey) return false;
  out.resize(KE_CM_GetKeyValueStringSize(valueKey));
  return KE_CM_GetKeyValueString(valueKey, kstd::span(out.data(), out.size()));
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
kernel::IoResourceRequirementsList *LegacyIsaEnum_QueryResourceRequirements(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto *ctx = pdo->deviceExtension.as<LegacyReservationContext>();
  if (!ctx) return nullptr;

  auto *req = KE_RES_CreateRequirementsList(kernel::device_type::Unknown);
  if (!req) return nullptr;

  auto *alt = KE_RES_AddAlternative(req);
  if (!alt) return req;

  if (ctx->hasPort) {
    KE_RES_AddPort(alt, ctx->portStart, ctx->portLength, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (ctx->hasMemory) {
    KE_RES_AddMemory(alt, ctx->memoryStart, ctx->memoryLength, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (ctx->hasIrq) {
    KE_RES_AddInterrupt(alt, ctx->irq, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (ctx->hasDma) {
    KE_RES_AddDma(alt, ctx->dma, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  return req;
}

// Reads one LegacyISAReservations\<childName> subkey. HwID is the only
// mandatory value (it becomes both the ProbeDevice dispatch key and the
// reported PDO's hardwareId); IoPorts/Memory/Irq/Dma/DriverImage are all
// optional, matching the fact that not every legacy device needs every kind
// of resource (e.g. a speaker has neither ports nor an image to load).
bool LoadLegacyReservation(
    const kernel::KConfigurationKeyPtr &deviceKey,
    const kstd::string_view &childName,
    kstd::string &outHwId,
    kstd::string &outDriverImage,
    LegacyReservationContext &ctx) {
  if (!ReadStringValue(deviceKey, "HwID", outHwId) || outHwId.empty()) {
    early_print_fmt("LegacyIsaEnum: '{}' has no HwID, skipping\r\n", childName);
    return false;
  }

  // DriverImage is optional: if absent, a driver already registered under
  // this HwID (e.g. loaded earlier as a Boot/Automatic driver) is expected
  // to bind instead.
  ReadStringValue(deviceKey, "DriverImage", outDriverImage);

  kstd::string ioPorts;
  if (ReadStringValue(deviceKey, "IoPorts", ioPorts)) {
    uint32_t start = 0, end = 0;
    if (ParseRange(ioPorts, start, end) && end >= start) {
      ctx.hasPort = true;
      ctx.portStart = start;
      ctx.portLength = end - start + 1;
    } else {
      early_print_fmt("LegacyIsaEnum: '{}' has invalid IoPorts '{}'\r\n", childName, ioPorts);
    }
  }

  kstd::string memory;
  if (ReadStringValue(deviceKey, "Memory", memory)) {
    uint32_t start = 0, end = 0;
    if (ParseRange(memory, start, end) && end >= start) {
      ctx.hasMemory = true;
      ctx.memoryStart = start;
      ctx.memoryLength = end - start + 1;
    } else {
      early_print_fmt("LegacyIsaEnum: '{}' has invalid Memory '{}'\r\n", childName, memory);
    }
  }

  kstd::string irq;
  if (ReadStringValue(deviceKey, "Irq", irq)) {
    if (ParseUInt32(irq, ctx.irq)) {
      ctx.hasIrq = true;
    } else {
      early_print_fmt("LegacyIsaEnum: '{}' has invalid Irq '{}'\r\n", childName, irq);
    }
  }

  kstd::string dma;
  if (ReadStringValue(deviceKey, "Dma", dma)) {
    if (ParseUInt32(dma, ctx.dma)) {
      ctx.hasDma = true;
    } else {
      early_print_fmt("LegacyIsaEnum: '{}' has invalid Dma '{}'\r\n", childName, dma);
    }
  }

  return true;
}

}// namespace

namespace kernel::driver {
void EnumerateLegacyISADevices() {
  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    early_print("LegacyIsaEnum: failed to open LocalMachine\r\n");
    return;
  }

  const auto legacyKey = KE_CM_OpenKey(localMachine, "LegacyISAReservations");
  if (!legacyKey) {
    early_print("LegacyIsaEnum: no LegacyISAReservations key, nothing to enumerate\r\n");
    return;
  }

  for (uint32_t index = 0;; ++index) {
    kstd::string_view childName;
    if (!KE_CM_EnumKey(legacyKey, index, childName)) {
      break;
    }

    const auto deviceKey = KE_CM_OpenKey(legacyKey, childName);
    if (!deviceKey) {
      continue;
    }

    kstd::string hwid;
    kstd::string driverImage;
    LegacyReservationContext ctx;
    if (!LoadLegacyReservation(deviceKey, childName, hwid, driverImage, ctx)) {
      continue;
    }

    if (!ProbeLegacyDevice(ctx)) {
      early_print_fmt("LegacyIsaEnum: '{}' (HwID={}) not detected\r\n", childName, hwid);
      continue;
    }

    // Load the device's own driver image (if any) up front, so it is
    // registered by the time the PnP worker thread matches the PDO's
    // hardware id.
    if (!driverImage.empty()) {
      KE_DRIVER_Load(driverImage);
    }

    const auto enumerator = kernel::CreateKObject<KDriverObject>("LegacyIsaEnum");
    if (!enumerator) {
      early_print_fmt("LegacyIsaEnum: failed to create enumerator driver object for '{}'\r\n", childName);
      continue;
    }
    enumerator->QueryResourceRequirements = LegacyIsaEnum_QueryResourceRequirements;

    auto pdo = KE_IO_CreateDeviceWithContext<LegacyReservationContext>(enumerator, {}, kernel::device_type::Unknown);
    if (!pdo) {
      early_print_fmt("LegacyIsaEnum: failed to create PDO for '{}'\r\n", childName);
      continue;
    }
    *pdo->deviceExtension.as<LegacyReservationContext>() = ctx;
    pdo->hardwareId = hwid;

    early_print_fmt("LegacyIsaEnum: '{}' (HwID={}) detected, reporting PDO\r\n", childName, hwid);
    KE_PNP_ReportNewDevice(KE_PNP_GetRootDevice(), pdo);
  }
}
}// namespace kernel::driver
