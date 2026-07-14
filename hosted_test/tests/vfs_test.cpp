#include <catch2/catch_test_macros.hpp>

#include <kernel/io.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>

#include "ob_test_support.hpp"
#include "stdkrn.hpp"

#include "fake_disk.hpp"
#include "fake_disk_file_backend.hpp"

#include <array.hpp>
#include <static_string.hpp>
#include <strfmt.hpp>
#include <cstdio>

namespace {

kstd::static_string<128> MakeTempImagePath(const char *name) {
  kstd::static_string<128> path;
  kstd::format(path, "/tmp/undos_vfs_test_{}.img", name);
  return path;
}

// A minimal test filesystem driver whose CreateHandle/ReadHandle/WriteHandle/
// SeekHandle delegate straight to KE_IO_ReadDevice/KE_IO_WriteDevice on the
// mounted volume, exercising the vfs.cpp <-> io.cpp <-> disk_fake path.
//
// cfunc is a plain C-style function pointer with no capture support, so any
// state the callbacks need to observe is tracked in this global (tests run
// sequentially, and each TestFsFixture resets it on construction).
struct TestFsDriverState {
  bool mountVolumeCalled = false;
  kernel::KDevicePtr<kernel::KDevice> lastMountedVolume;
  kstd::static_string<64> lastMountOptions;
};

TestFsDriverState g_testFsState;

bool TestFs_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kstd::string_view &options) {
  g_testFsState.mountVolumeCalled = true;
  g_testFsState.lastMountedVolume = mount->volume;
  g_testFsState.lastMountOptions = options;
  return true;
}

bool TestFs_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, kernel::KVFSNode &out) {
  out.type = kernel::VFSNodeType::Directory;
  out.size = 0;
  return true;
}

bool TestFs_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &, kstd::string_view, kernel::KVFSNode &out) {
  out.type = kernel::VFSNodeType::File;
  out.size = 0;
  return true;
}

bool TestFs_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &, kernel::KFileObject::OpenMode) {
  return true;
}

uint64_t TestFs_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &, uint64_t offset, const kstd::span<uint8_t> &buffer) {
  size_t transferred = 0;
  const auto status = KE_IO_ReadDevice(mount->volume, offset, buffer, transferred);
  if (status != kernel::IoStatus::Success) return 0;
  return transferred;
}

uint64_t TestFs_WriteHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &, uint64_t offset, const kstd::span<const uint8_t> &buffer) {
  size_t transferred = 0;
  const auto status = KE_IO_WriteDevice(mount->volume, offset, buffer, transferred);
  if (status != kernel::IoStatus::Success) return 0;
  return transferred;
}

// Bundles everything a test needs to register a fresh filesystem driver and
// a fresh disk_fake-backed volume; destructor order matters here (VFS state
// keeps references to these), so callers must keep this object alive for the
// duration of the mount/file operations.
struct TestFsFixture {
  explicit TestFsFixture(const char *imageName, size_t imageSize = 4096)
      : imagePath(MakeTempImagePath(imageName)),
        backing(kstd::string_view(imagePath)) {
    // The filesystem namespace is a process-wide singleton with no unregister
    // support, so each fixture must use a unique name to avoid colliding with
    // filesystem drivers registered by earlier tests.
    kstd::format(fsName, "TestFs_{}", imageName);

    backing.Resize(imageSize);

    g_testFsState = TestFsDriverState{};

    driver = kernel::CreateKObject<kernel::KDriverObject>("TestFsDriver");
    driver->Read = diskfake::Read;
    driver->Write = diskfake::Write;

    kstd::static_string<64> volumeName;
    kstd::format(volumeName, "TestFsVolume_{}", imageName);
    volume = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(driver, volumeName, kernel::device_type::Disk);
    volume->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

    filesystem = KE_VFS_RegisterFilesystemDriver(driver, fsName);
    if (filesystem) {
      filesystem->MountVolume = TestFs_MountVolume;
      filesystem->GetRootNode = TestFs_GetRootNode;
      filesystem->Lookup = TestFs_Lookup;
      filesystem->CreateHandle = TestFs_CreateHandle;
      filesystem->ReadHandle = TestFs_ReadHandle;
      filesystem->WriteHandle = TestFs_WriteHandle;
    }
  }

  ~TestFsFixture() {
    // The VFS directory is a process-wide singleton with no unmount API, so
    // explicitly detach the mount created by Mount() to avoid leaking it into
    // later, unrelated test cases that mount at the same path.
    if (!mountedAt.empty()) {
      if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), mountedAt)) {
        KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
      }
    }
    std::remove(imagePath.data());
  }

  bool Mount(const kstd::string_view &mountPoint, const kstd::string_view &options) {
    if (KE_VFS_Mount(volume, fsName, mountPoint, options)) {
      mountedAt = mountPoint;
      return true;
    }
    return false;
  }

  kstd::static_string<64> fsName = "TestFs";
  kstd::static_string<128> imagePath;
  kstd::static_string<32> mountedAt;
  diskfake::FileBackingStore backing;
  kernel::KObjectPtr<kernel::KDriverObject> driver;
  kernel::KDevicePtr<kernel::KDevice> volume;
  kernel::KObjectPtr<kernel::KFilesystemObject> filesystem;
};

}// namespace

TEST_CASE("KE_VFS_RegisterFilesystemDriver / UnregisterFilesystemDriver", "[vfs]") {
  EnsureObjectManagerInitialized();

  SECTION("Registration succeeds and is findable") {
    TestFsFixture fixture("register_ok");
    REQUIRE(fixture.filesystem != nullptr);
    REQUIRE(KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemRoot(), fixture.fsName).get() == fixture.filesystem.get());
  }

  SECTION("Duplicate filesystem name is rejected") {
    TestFsFixture fixture("register_dup");
    REQUIRE(fixture.filesystem != nullptr);

    const auto duplicate = KE_VFS_RegisterFilesystemDriver(fixture.driver, fixture.fsName);
    REQUIRE(duplicate == nullptr);
  }
}

TEST_CASE("KE_VFS_Mount", "[vfs]") {
  EnsureObjectManagerInitialized();

  SECTION("Mounting at root succeeds when no mount exists") {
    TestFsFixture fixture("mount_root");
    REQUIRE(fixture.Mount("/", ""));

    REQUIRE(g_testFsState.mountVolumeCalled);
    REQUIRE(g_testFsState.lastMountedVolume.get() == fixture.volume.get());
  }

  SECTION("Mounting at an already-used mount point is rejected") {
    TestFsFixture fixture("mount_dup");
    REQUIRE(fixture.Mount("/", ""));
    REQUIRE_FALSE(fixture.Mount("/", ""));
  }

  SECTION("Mounting at a non-root path with no parent mount is rejected") {
    TestFsFixture fixture("mount_norootparent");
    REQUIRE_FALSE(fixture.Mount("/nested", ""));
  }

  SECTION("The filesystem's MountVolume callback receives the correct volume and options") {
    TestFsFixture fixture("mount_options");
    REQUIRE(fixture.Mount("/", "opt=value"));

    REQUIRE(g_testFsState.lastMountedVolume.get() == fixture.volume.get());
    REQUIRE(g_testFsState.lastMountOptions == "opt=value");
  }
}

TEST_CASE("KE_VFS_OpenFile / ReadFile / WriteFile / SeekFile / TellFile / CloseFile round-trip", "[vfs]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("filerw");
  REQUIRE(fixture.Mount("/", ""));

  auto file = KE_VFS_OpenFile("/somefile.txt", kernel::KFileObject::OpenMode::ReadWrite);
  REQUIRE(file != nullptr);
  REQUIRE(KE_VFS_TellFile(file) == 0);

  kstd::array<uint8_t, 16> writeBuf{};
  for (size_t i = 0; i < writeBuf.size(); i++) writeBuf[i] = static_cast<uint8_t>('A' + i);

  const kstd::span<uint8_t> writeSpan(writeBuf);
  const auto written = KE_VFS_WriteFile(file, writeSpan);
  REQUIRE(written == writeBuf.size());
  REQUIRE(KE_VFS_TellFile(file) == writeBuf.size());

  REQUIRE(KE_VFS_SeekFile(file, 0) == 0);
  REQUIRE(KE_VFS_TellFile(file) == 0);

  kstd::array<uint8_t, 16> readBuf{};
  kstd::span<uint8_t> readSpan(readBuf);
  const auto read = KE_VFS_ReadFile(file, readSpan);
  REQUIRE(read == readBuf.size());
  REQUIRE(KE_VFS_TellFile(file) == readBuf.size());

  for (size_t i = 0; i < readBuf.size(); i++) {
    REQUIRE(readBuf[i] == writeBuf[i]);
  }

  // KE_VFS_CloseFile takes the handle by rvalue reference but does not null
  // out the caller's variable, so we can't assert `file == nullptr` here;
  // just verify the close call completes without issue.
  KE_VFS_CloseFile(kstd::move(file));
}
