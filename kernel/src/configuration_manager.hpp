
#pragma once

#include <string_view.hpp>

namespace kernel {

namespace configuration_manager {
void init();

// Rebinds `hiveName`'s backing file from its current path to `newFilePath`,
// keeping the existing in-memory KConfigurationRoot::roots index intact
// (valid because the new path resolves to the exact same underlying bytes
// on disk, just reached through a different VFS mount once the real root
// filesystem is up). Returns false if the hive isn't registered or the new
// file can't be opened (see implementation for the exact failure handling).
bool RebindHiveFile(const kstd::string_view &hiveName, const kstd::string_view &newFilePath);

// Once the real root filesystem is mounted, rebinds the "LocalMachine" hive
// from its early-boot BootDeviceVolume path onto the writable root-mount
// path "/System/Config/LocalMachine". The in-memory roots index is kept
// intact (same on-disk bytes, different VFS access path) - there is no
// longer a separate volatile "Boot" hive to copy from.
void PromoteBootHive();
}
}// namespace kernel
