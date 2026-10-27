#include "root_volume_selector.hpp"

#include <kernel/device.hpp>

namespace kernel::root_volume_selector {
namespace {

int HexDigit(char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

}// namespace

bool ParseVolumeId(kstd::string_view text, uint32_t &id) noexcept {
  if (text.size() != 9 || text[4] != '-') return false;

  uint32_t parsed = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == 4) continue;
    const int digit = HexDigit(text[i]);
    if (digit < 0) return false;
    parsed = (parsed << 4) | static_cast<uint32_t>(digit);
  }

  id = parsed;
  return true;
}

Result FindVolumeById(
    const KObjectPtr<KDirectoryObject> &deviceDirectory,
    uint32_t id,
    const KVolumeIdProbe &probe) {
  Result result{};
  if (!deviceDirectory || !probe) {
    result.code = ResultCode::ProbeError;
    result.error = KernelError::InvalidArgument;
    return result;
  }

  for (const auto &entry: deviceDirectory->child_list) {
    if (entry->type != KDeviceObject::Type) continue;
    const KObjectPtr<KDeviceObject> deviceObject(static_cast<KDeviceObject *>(entry));
    if (!deviceObject || !deviceObject->device ||
        deviceObject->device->deviceType != device_type::Volume) {
      continue;
    }

    const auto &device = deviceObject->device;
    if (!device->driverObject || !device->driverObject->Read) continue;

    uint32_t candidateId = 0;
    const auto candidate = probe(device, candidateId);
    if (!candidate) {
      result.code = ResultCode::ProbeError;
      result.failingDeviceName = deviceObject->name;
      result.error = candidate.error();
      return result;
    }
    if (!*candidate || candidateId != id) continue;

    ++result.matchCount;
    if (result.matchCount == 1) result.deviceObject = deviceObject;
  }

  if (result.matchCount == 1) {
    result.code = ResultCode::Selected;
  } else if (result.matchCount > 1) {
    result.code = ResultCode::Ambiguous;
    result.deviceObject = nullptr;
  } else {
    result.code = ResultCode::NoMatch;
  }
  return result;
}

}// namespace kernel::root_volume_selector
