#include <catch2/catch_test_macros.hpp>

#include <kernel/configuration_manager.hpp>
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
#include <stdlib.h>
#include <unistd.h>

namespace {

using DriverEntryFn = void (*)(kernel::KObjectPtr<kernel::KDriverObject> &, const kernel::KConfigurationKeyPtr &);

struct FailingWrites final : diskfake::IBackingStore {
  explicit FailingWrites(diskfake::IBackingStore &store) : backing(store) {}
  diskfake::IBackingStore &backing;
  kstd::expected<size_t, kstd::error_code> Size() const override { return backing.Size(); }
  kstd::error_code Resize(size_t size) override { return backing.Resize(size); }
  kstd::error_code ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) override { return backing.ReadAt(offset, buffer); }
  kstd::error_code WriteAt(uint64_t, kstd::span<const uint8_t>) override { return kernel::KernelError::IoError; }
};

kstd::static_string<128> MakeTempImagePath() {
  char path[] = "/tmp/undos-vfs-fat-XXXXXX";
  const int descriptor = mkstemp(path);
  REQUIRE(descriptor >= 0);
  REQUIRE(close(descriptor) == 0);
  return kstd::static_string<128>(path);
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
  const kstd::span<uint8_t> mbr(disk.data(), kSectorSize);
  const kstd::span<uint8_t> entry0 = mbr.subspan(446);
  entry0[4] = 0x01;// FAT12 partition type
  PutLe32(entry0.subspan(8), kPartitionStartLba);
  PutLe32(entry0.subspan(12), kPartitionSectors);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  // --- FAT12 BPB (partition-relative LBA 0, i.e. disk LBA 1) ---
  const kstd::span<uint8_t> bpb(disk.data() + kPartitionStartLba * kSectorSize, kSectorSize);
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

// Test-local volatile hive standing in for driver_loader.cpp's "Boot" hive,
// so LoadDriver() below can hand each driver a per-driver configuration key
// the same way KE_DRIVER_Load() does for a real boot driver.
kernel::KConfigurationKeyPtr GetTestBootRoot() {
  if (auto existing = KE_CM_OpenRoot("IntegrationTestBoot").value_or(nullptr)) {
    return existing;
  }
  return KE_CM_OpenRoot("IntegrationTestBoot").value_or(nullptr);
}

// Loads a driver module and calls its DriverEntry with a freshly created
// KDriverObject, mirroring what driver_loader.cpp would do for a real ELF
// driver image.
kernel::KObjectPtr<kernel::KDriverObject> LoadDriver(const char *path, const char *driverName) {
  void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  REQUIRE(handle != nullptr);

  auto *entry = reinterpret_cast<DriverEntryFn>(dlsym(handle, "DriverEntry"));
  REQUIRE(entry != nullptr);

  auto driver = kernel::CreateKObject<kernel::KDriverObject>(driverName).value_or(nullptr);

  kernel::KConfigurationKeyPtr driverConfig = nullptr;
  if (const auto bootRoot = GetTestBootRoot()) {
    driverConfig = KE_CM_CreateKey(bootRoot, driverName, kernel::KConfigurationKeyAccess::ReadWrite).value_or(nullptr);
  }

  entry(driver, driverConfig);
  return driver;
}

}// namespace

TEST_CASE("End-to-end disk_fake -> volmgr -> fs_fat -> VFS mount", "[vfs][fat][integration]") {
  EnsureObjectManagerInitialized();

  const auto imagePath = MakeTempImagePath();
  const kstd::string_view imagePathView(imagePath);
  diskfake::FileBackingStore backing(imagePathView);
  REQUIRE(backing.IsOpen());

  const auto diskImage = BuildFat12DiskImage();
  REQUIRE(diskfake::Seed(diskfake::FakeDiskContext{}, kstd::span<const uint8_t>(diskImage)) == kernel::KernelError::InvalidState);// no backing set, sanity-check Seed's guard
  REQUIRE_FALSE(backing.Resize(diskImage.size()));
  REQUIRE_FALSE(backing.WriteAt(0, kstd::span<const uint8_t>(diskImage)));

  UNSCOPED_INFO("Disk image left on disk for inspection at: " << imagePath.data());

  // --- disk_fake: a Disk PDO backed by the image file we just wrote ---
  auto diskDriver = kernel::CreateKObject<kernel::KDriverObject>("IntegrationDiskDriver").value_or(nullptr);
  diskDriver->Read = diskfake::Read;
  diskDriver->Write = diskfake::Write;

  static unsigned fixtureNumber = 0;
  kstd::static_string<64> diskName;
  kstd::format(diskName, "IntegrationDisk{}", ++fixtureNumber);
  auto diskDevice = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(diskDriver, diskName, kernel::device_type::Disk).value_or(nullptr);
  REQUIRE(diskDevice != nullptr);
  diskDevice->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

  // --- volmgr: load as a standalone module and drive AddDevice/StartDevice directly ---
  auto volmgrDriver = LoadDriver(VOLMGR_DRIVER_PATH, "IntegrationVolMgrDriver");
  REQUIRE(volmgrDriver->AddDevice.valid());
  REQUIRE(volmgrDriver->StartDevice.valid());

  REQUIRE_FALSE(volmgrDriver->AddDevice(volmgrDriver, diskDevice));
  g_lastReportedPdo = nullptr;
  volmgrDriver->StartDevice(diskDevice);

  const auto volumePdo = g_lastReportedPdo;
  REQUIRE(volumePdo != nullptr);
  REQUIRE(volumePdo->deviceType == kernel::device_type::Volume);

  // --- fs_fat: load as a standalone module; its DriverEntry registers "FAT" with the VFS ---
  LoadDriver(FS_FAT_DRIVER_PATH, "IntegrationFatDriver");

  // KE_VFS_Mount only permits mounting at a non-root path when a parent mount
  // already exists, so mount this standalone volume at the root "/".
  REQUIRE_FALSE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));

  SECTION("Directory browsing and persistent short-name file management") {
    using kernel::KernelError;
    using kernel::VFSFileOperation;
    kstd::array<uint8_t, 512> root{};
    const char sourceName[] = "SOURCE  TXT";
    for (size_t i = 0; i < 11; ++i) root[i] = static_cast<uint8_t>(sourceName[i]);
    root[11] = 32;
    PutLe16(kstd::span(root).subspan(26), 2);
    PutLe32(kstd::span(root).subspan(28), 700);
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    kstd::array<uint8_t, 512> fat{};
    fat[0] = 0xf8; fat[1] = fat[2] = 0xff;
    fat[3] = 3; fat[4] = 0xf0; fat[5] = 0xff; // cluster 2 -> 3 -> EOC
    REQUIRE_FALSE(backing.WriteAt(2 * kSectorSize, fat));
    kstd::array<uint8_t, 1024> data{};
    for (size_t i = 0; i < 700; ++i) data[i] = static_cast<uint8_t>(i * 17);
    REQUIRE_FALSE(backing.WriteAt(4 * kSectorSize, data));

    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    auto found = KE_VFS_ReadDirectory(*directory, 0, entry);
    REQUIRE(found);
    REQUIRE(*found);
    REQUIRE(kstd::string_view(entry.name) == "SOURCE.TXT");
    REQUIRE(entry.size == 700);
    REQUIRE(entry.attributes == 32);
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 1, entry));
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, UINT32_MAX, entry));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/COPY.TXT") == KernelError::ResourceConflict);
    REQUIRE(KE_VFS_Unmount("/") == KernelError::ResourceConflict);
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE(KE_VFS_ReadDirectory(*directory, 0, entry).error() == KernelError::InvalidState);
    REQUIRE(KE_VFS_ReadDirectory(nullptr, 0, entry).error() == KernelError::InvalidHandle);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/../SOURCE.TXT", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/SOURCE.TXT", "/other") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(static_cast<VFSFileOperation>(99), "/SOURCE.TXT", "") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/SOURCE.TXT") == KernelError::AlreadyExists);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/bad*name") == KernelError::InvalidArgument);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/TOOLONGNAME.TXT") == KernelError::NameTooLong);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/SOURCE.TXT", "/COPY.TXT"));
    auto copied = KE_VFS_OpenFile("/COPY.TXT", kernel::KIOObject::OpenMode::Read);
    REQUIRE(copied);
    REQUIRE((*copied)->vfsNode->size == 700);
    kstd::array<uint8_t, 1024> read{};
    REQUIRE(*KE_VFS_ReadFile(*copied, read) == 700);
    for (size_t i = 0; i < 700; ++i) REQUIRE(read[i] == data[i]);
    REQUIRE_FALSE(KE_VFS_CloseFile(*copied));
    auto unrelated = KE_VFS_OpenFile("/./source.txt", kernel::KIOObject::OpenMode::ReadWrite);
    REQUIRE(unrelated);
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/SOURCE.TXT", "") == KernelError::ResourceConflict);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/FOLDER", ""));
    REQUIRE_FALSE(KE_VFS_CloseFile(*unrelated));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/FOLDER", "/OTHER") == KernelError::Unsupported);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/COPY.TXT", "/FOLDER/MOVED.TXT"));
    REQUIRE_FALSE(KE_VFS_OpenFile("/COPY.TXT", kernel::KIOObject::OpenMode::Read));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/FOLDER", "") == KernelError::AccessDenied);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/FOLDER/MOVED.TXT", "/FOLDER/NEW.TXT"));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/FOLDER/NEW.TXT", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Move, "/FOLDER", "/RENAMED"));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/RENAMED", ""));
    // Remounting proves that enumeration and content came from persisted sectors.
    REQUIRE_FALSE(KE_VFS_Unmount("/"));
    REQUIRE_FALSE(KE_VFS_Mount(volumePdo, "FAT", "/", ""));
    directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "SOURCE.TXT");
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 1, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE_FALSE(backing.ReadAt(2 * kSectorSize, read));
    REQUIRE(read[3] == 3); // source chain is unchanged
    REQUIRE(read[4] == 0xf0);
    REQUIRE(read[5] == 0xff);
    for (size_t i = 6; i < 54; ++i) REQUIRE(read[i] == 0); // allocated clusters reclaimed
  }

  SECTION("FAT32 allocation mirrors and FSInfo invalidation") {
    using kernel::VFSFileOperation;
    REQUIRE_FALSE(KE_VFS_Unmount("/"));
    constexpr uint32_t reserved = 32, fatSectors = 512, clusters = 65525;
    constexpr uint32_t dataSector = reserved + 2 * fatSectors;
    constexpr uint32_t sectors = dataSector + clusters;
    REQUIRE_FALSE(backing.Resize(static_cast<size_t>(sectors) * kSectorSize));
    kstd::array<uint8_t, 512> bpb{};
    PutLe16(kstd::span(bpb).subspan(11), 512);
    bpb[13] = 1;
    PutLe16(kstd::span(bpb).subspan(14), reserved);
    bpb[16] = 2;
    PutLe32(kstd::span(bpb).subspan(32), sectors);
    PutLe32(kstd::span(bpb).subspan(36), fatSectors);
    PutLe32(kstd::span(bpb).subspan(44), 2);
    PutLe16(kstd::span(bpb).subspan(48), 1);
    PutLe16(kstd::span(bpb).subspan(50), 6);
    REQUIRE_FALSE(backing.WriteAt(0, bpb));
    kstd::array<uint8_t, 512> fat{};
    PutLe32(kstd::span(fat), 0x0ffffff8);
    PutLe32(kstd::span(fat).subspan(4), 0xffffffff);
    PutLe32(kstd::span(fat).subspan(8), 0x0fffffff);
    REQUIRE_FALSE(backing.WriteAt(reserved * kSectorSize, fat));
    REQUIRE_FALSE(backing.WriteAt((reserved + fatSectors) * kSectorSize, fat));
    kstd::array<uint8_t, 512> info{};
    PutLe32(kstd::span(info), 0x41615252);
    PutLe32(kstd::span(info).subspan(484), 0x61417272);
    PutLe32(kstd::span(info).subspan(488), clusters - 1);
    PutLe32(kstd::span(info).subspan(492), 3);
    REQUIRE_FALSE(backing.WriteAt(kSectorSize, info));
    REQUIRE_FALSE(backing.WriteAt(7 * kSectorSize, info));
    auto raw = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(diskDriver, "RawFat32", kernel::device_type::Disk);
    REQUIRE(raw);
    (*raw)->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;
    REQUIRE_FALSE(KE_VFS_Mount(*raw, "FAT", "/", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/TESTDIR", ""));
    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    REQUIRE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE(kstd::string_view(entry.name) == "TESTDIR");
    REQUIRE(entry.attributes & 16);
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    kstd::array<uint8_t, 512> mirror{};
    REQUIRE_FALSE(backing.ReadAt(reserved * kSectorSize, fat));
    REQUIRE_FALSE(backing.ReadAt((reserved + fatSectors) * kSectorSize, mirror));
    for (size_t i = 0; i < fat.size(); ++i) REQUIRE(fat[i] == mirror[i]);
    REQUIRE(fat[12] == 0xff); // cluster 3 allocated
    REQUIRE_FALSE(backing.ReadAt(kSectorSize, info));
    for (size_t i = 488; i < 496; ++i) REQUIRE(info[i] == 0xff);
    REQUIRE_FALSE(backing.ReadAt(7 * kSectorSize, info));
    for (size_t i = 488; i < 496; ++i) REQUIRE(info[i] == 0xff);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/TESTDIR", ""));
    REQUIRE_FALSE(backing.ReadAt(reserved * kSectorSize, fat));
    for (size_t i = 12; i < 16; ++i) REQUIRE(fat[i] == 0);
  }

  SECTION("Directory growth, empty files and mutation boundaries") {
    using kernel::KernelError;
    using kernel::VFSFileOperation;
    kstd::array<uint8_t, 512> root{};
    const char seed[] = "EMPTY   TXT";
    for (size_t i = 0; i < 11; ++i) root[i] = static_cast<uint8_t>(seed[i]);
    root[11] = 32;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::CreateDirectory, "/DIR", ""));
    for (int i = 0; i < 20; ++i) {
      char name[] = "/DIR/F00.TXT";
      name[6] = static_cast<char>('0' + i / 10);
      name[7] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", name));
    }
    auto directory = KE_VFS_OpenFile("/DIR", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    for (uint32_t i = 0; i < 20; ++i) {
      REQUIRE(*KE_VFS_ReadDirectory(*directory, i, entry));
      REQUIRE(entry.size == 0);
    }
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 20, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Move, "/DIR", "/DIR/NESTED") == KernelError::Unsupported);
    for (int i = 0; i < 20; ++i) {
      char name[] = "/DIR/F00.TXT";
      name[6] = static_cast<char>('0' + i / 10);
      name[7] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, name, ""));
    }
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/DIR", ""));
    // The fixed FAT12 root cannot grow: source plus 15 copies fills its 16 slots.
    for (int i = 0; i < 15; ++i) {
      char name[] = "/R00.TXT";
      name[2] = static_cast<char>('0' + i / 10);
      name[3] = static_cast<char>('0' + i % 10);
      REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", name));
    }
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", "/FULL.TXT") == KernelError::ResourceLimitExceeded);
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/R00.TXT", ""));
    REQUIRE_FALSE(KE_VFS_OperateFile(VFSFileOperation::Copy, "/EMPTY.TXT", "/REUSED.TXT"));
    REQUIRE_FALSE(backing.ReadAt(3 * kSectorSize, root));
    root[11] |= 1;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Delete, "/EMPTY.TXT", "") == KernelError::AccessDenied);
    // An LFN fragment before the short entry must not be left dangling by rename.
    root[11] &= static_cast<uint8_t>(~1u);
    for (size_t i = 0; i < 32; ++i) root[32 + i] = root[i];
    root[0] = 0x41;
    root[11] = 0x0f;
    REQUIRE_FALSE(backing.WriteAt(3 * kSectorSize, root));
    REQUIRE(KE_VFS_OperateFile(VFSFileOperation::Move, "/EMPTY.TXT", "/NEW.TXT") == KernelError::Unsupported);
  }

  SECTION("Device write failures are explicit and disable later mutations") {
    FailingWrites failures(backing);
    auto *context = diskDevice->deviceExtension.as<diskfake::FakeDiskContext>();
    context->backing = &failures;
    const auto result = KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/FAIL", "");
    context->backing = &backing;
    REQUIRE(result == kernel::KernelError::IoError);
    REQUIRE(KE_VFS_OperateFile(kernel::VFSFileOperation::CreateDirectory, "/RETRY", "") == kernel::KernelError::Unsupported);
    auto directory = KE_VFS_OpenFile("/", kernel::KIOObject::OpenMode::Read);
    REQUIRE(directory);
    kernel::KVFSDirectoryEntry entry{};
    REQUIRE_FALSE(*KE_VFS_ReadDirectory(*directory, 0, entry));
    REQUIRE_FALSE(KE_VFS_CloseFile(*directory));
  }

  // Clean up the mount so later tests mounting at "/" aren't blocked by this
  // one (the singleton VFS directory persists for the whole process).
  if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/")) {
    KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
  }

  std::remove(imagePath.data());
}
