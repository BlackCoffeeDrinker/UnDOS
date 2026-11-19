#pragma once

#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDirectoryObject.hpp>
#include <kernel/kobject/KFilesystemObject.hpp>

namespace kernel::root_volume_selector {

enum class ResultCode : uint8_t {
  Selected,
  NoMatch,
  Ambiguous,
  ProbeError,
};

struct Result {
  ResultCode code{ResultCode::NoMatch};
  KObjectPtr<KDeviceObject> deviceObject{};
  uint32_t matchCount{0};
  kstd::string_view failingDeviceName{};
  kstd::error_code error{};
};

bool ParseVolumeId(kstd::string_view text, uint32_t &id) noexcept;

Result FindVolumeById(
    const KObjectPtr<KDirectoryObject> &deviceDirectory,
    uint32_t id,
    const KVolumeIdProbe &probe);

}// namespace kernel::root_volume_selector
