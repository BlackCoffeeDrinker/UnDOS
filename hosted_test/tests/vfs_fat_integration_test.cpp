#include <catch2/catch_test_macros.hpp>

#include <kernel/io.hpp>
#include <kernel/vfs.hpp>

#include "ob_test_support.hpp"
#include "pnp_test_hooks.hpp"
#include "stdkrn.hpp"

#include "fake_disk.hpp"
#include "fake_disk_file_backend.hpp"

#include <array.hpp>
#include <static_string.hpp>
#include <strfmt.hpp>

#include <cstdio>
#include <dlfcn.h>

namespace {

using DriverEntryFn = void (*)(kernel::KObjectPtr<kernel::KDriverObject> &);

kstd::static_string<128> MakeTempImagePath(const char *name) {
  kstd::static_string<128> path;
  kstd::format(path, "/tmp/undos_vfs_fat_integration_{}.img", name);
  return path;
}

void PutLe32(kstd::span<uint8_t> p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void PutLe16(kstd::span<uint8_t> p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kPartitionStartLba = 1;
constexpr uint32_t kPartitionSectors = 39;// 1 (BPB) + 1 (FAT) + 1 (root dir) + 36 (data)
constexpr uint32_t kDiskSectors = kPartitionStartLba + kPartitionSectors;

// Builds a full disk image: MBR at LBA 0 with a single FAT12 partition at
// LBA 1, followed by a minimal-but-valid FAT12 BPB at the start of that
// partition (sectorsPerCluster=1, numFATs=1, a tiny root directory), so
// fat.cpp's FAT_MountVolume detects FAT12 and reports success.
kstd::array<uint8_t, kDiskSectors * kSectorSize> BuildFat12DiskImage() {
  kstd::array<uint8_t, kDiskSectors * kSectorSize> disk{};

  // --- MBR (LBA 0) ---
  kstd::span<uint8_t> mbr(disk.data(), kSectorSize);
  kstd::span<uint8_t> entry0 = mbr.subspan(446);
  entry0[4] = 0x01;// FAT12 partition type
  PutLe32(entry0.subspan(8), kPartitionStartLba);
  PutLe32(entry0.subspan(12), kPartitionSectors);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  // --- FAT12 BPB (partition-relative LBA 0, i.e. disk LBA 1) ---
  kstd::span<uint8_t> bpb(disk.data() + kPartitionStartLba * kSectorSize, kSectorSize);
  bpb[0] = 0xEB;// jmpBoot
  bpb[1] = 0x3C;
  bpb[2] = 0x90;
  const char oem[8] = {'U', 'N', 'D', 'O', 'S', ' ', ' ', ' '};
  for (size_t i = 0; i < 8; i++) bpb[3 + i] = static_cast<uint8_t>(oem[i]);
  PutLe16(bpb.subspan(11), kSectorSize);// bytesPerSector
  bpb[13] = 1;                          // sectorsPerCluster
  PutLe16(bpb.subspan(14), 1);          // reservedSectors
  bpb[16] = 1;                          // numFATs
  PutLe16(bpb.subspan(17), 16);         // rootEntryCount (-> 1 root dir sector)
  PutLe16(bpb.subspan(19), static_cast<uint16_t>(kPartitionSectors));// totalSectors16
  bpb[21] = 0xF8;                                                    // media
  PutLe16(bpb.subspan(22), 1);                                       // fatSize16

  return disk;
}

// Loads a driver module and calls its DriverEntry with a freshly created
// KDriverObject, mirroring what driver_loader.cpp would do for a real ELF
// driver image.
kernel::KObjectPtr<kernel::KDriverObject> LoadDriver(const char *path, const char *driverName) {
  void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  REQUIRE(handle != nullptr);

  auto *entry = reinterpret_cast<DriverEntryFn>(dlsym(handle, "DriverEntry"));
  REQUIRE(entry != nullptr);

  auto driver = kernel::CreateKObject<kernel::KDriverObject>(driverName);
  entry(driver);
  return driver;
}

}// namespace

TEST_CASE("End-to-end disk_fake -> volmgr -> fs_fat -> VFS mount", "[vfs][fat][integration]") {
  EnsureObjectManagerInitialized();

  const auto imagePath = MakeTempImagePath("mount");
  const kstd::string_view imagePathView(imagePath);
  diskfake::FileBackingStore backing(imagePathView);
  REQUIRE(backing.IsOpen());

  const auto diskImage = BuildFat12DiskImage();
  REQUIRE(diskfake::Seed(diskfake::FakeDiskContext{}, kstd::span<const uint8_t>(diskImage)) == false);// no backing set, sanity-check Seed's guard
  REQUIRE(backing.Resize(diskImage.size()));
  REQUIRE(backing.WriteAt(0, kstd::span<const uint8_t>(diskImage)));

  UNSCOPED_INFO("Disk image left on disk for inspection at: " << imagePath.data());

  // --- disk_fake: a Disk PDO backed by the image file we just wrote ---
  auto diskDriver = kernel::CreateKObject<kernel::KDriverObject>("IntegrationDiskDriver");
  diskDriver->Read = diskfake::Read;
  diskDriver->Write = diskfake::Write;

  auto diskDevice = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(diskDriver, "IntegrationDisk", kernel::device_type::Disk);
  REQUIRE(diskDevice != nullptr);
  diskDevice->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

  // --- volmgr: load as a standalone module and drive AddDevice/StartDevice directly ---
  auto volmgrDriver = LoadDriver(VOLMGR_DRIVER_PATH, "IntegrationVolMgrDriver");
  REQUIRE(volmgrDriver->AddDevice.valid());
  REQUIRE(volmgrDriver->StartDevice.valid());

  REQUIRE(volmgrDriver->AddDevice(volmgrDriver, diskDevice));
  g_lastReportedPdo = nullptr;
  volmgrDriver->StartDevice(diskDevice);

  const auto volumePdo = g_lastReportedPdo;
  REQUIRE(volumePdo != nullptr);
  REQUIRE(volumePdo->deviceType == kernel::device_type::Volume);

  // --- fs_fat: load as a standalone module; its DriverEntry registers "FAT" with the VFS ---
  LoadDriver(FS_FAT_DRIVER_PATH, "IntegrationFatDriver");

  // KE_VFS_Mount only permits mounting at a non-root path when a parent mount
  // already exists, so mount this standalone volume at the root "/".
  REQUIRE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));

  // Clean up the mount so later tests mounting at "/" aren't blocked by this
  // one (the singleton VFS directory persists for the whole process).
  if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/")) {
    KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
  }

  std::remove(imagePath.data());
}
