#include "fake_disk.hpp"
#include "fake_disk_memory_backend.hpp"

#include <Kernel.hpp>

namespace {

// Owns the in-memory backing store for the disk_fake PDO created by
// Fake_AddDevice. There is only ever a single fake disk on the freestanding
// target, so a static instance is sufficient.
diskfake::MemoryBackingStore g_backing;

// Creates the Disk PDO and attaches it as a filter over `pdo` (mirroring
// Ata_AddDevice's filter-over-parent pattern), so callers can retrieve it via
// `pdo->attachedDevice` afterward.
bool Fake_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto disk = KE_IO_CreateBusWithContext<diskfake::FakeDiskContext>(driver, "FakeDisk");
  if (!disk) {
    early_print("disk_fake: Failed to create Disk PDO\r\n");
    return false;
  }

  disk->deviceType = kernel::device_type::Disk;
  disk->hardwareId = "disk";

  auto *ctx = disk->deviceExtension.as<diskfake::FakeDiskContext>();
  ctx->backing = &g_backing;

  KE_IO_AttachFilterDevice(disk, pdo);

  return true;
}

void Fake_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  early_print_fmt("Fake_StartDevice with PDO: {}\r\n", pdo->Name());
}

}// namespace

UNDOS_DRIVER_ENTRY {
  driver->AddDevice = Fake_AddDevice;
  driver->StartDevice = Fake_StartDevice;
  driver->Read = diskfake::Read;
  driver->Write = diskfake::Write;
}
