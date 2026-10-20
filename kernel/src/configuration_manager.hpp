
#pragma once

#include <kernel/kerror.hpp>
#include <string_view.hpp>

namespace kernel {

namespace configuration_manager {
void init();

// Rebinds `hiveName`'s backing file from its current path to `newFilePath`,
// keeping the existing in-memory KConfigurationRoot::roots index intact
// (valid because the new path resolves to the exact same underlying bytes
// on disk, just reached through a different VFS mount once the real root
// filesystem is up). Returns NotFound if the hive isn't registered, or the VFS error if the new
// file can't be opened (see implementation for the exact failure handling).
kstd::error_code RebindHiveFile(const kstd::string_view &hiveName, const kstd::string_view &newFilePath);

// Once the real root filesystem is mounted, rebinds the "LocalMachine" hive
// onto the same path "/System/Config/MACHI", now resolved through the
// freshly mounted writable root filesystem instead of the early-boot
// BootDeviceVolume that was previously mounted at "/". The in-memory roots
// index is kept intact (same on-disk bytes, different underlying volume).
void PromoteBootHive();
}
}// namespace kernel
