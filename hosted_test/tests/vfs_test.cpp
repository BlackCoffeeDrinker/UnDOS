#include <catch2/catch_test_macros.hpp>

#include <kernel/io.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/kobject/KProcessObject.hpp>
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
  kstd::format(path, "undos_vfs_test_{}.img", name);
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
  bool createHandleSucceeds = true;
  size_t closeCount = 0;
  kernel::KDevicePtr<kernel::KDevice> lastMountedVolume;
  kstd::static_string<64> lastMountOptions;
  bool unmountVolumeCalled = false;
  kernel::KObjectPtr<kernel::KVolumeMountObject> lastUnmountedMount;
};

TestFsDriverState g_testFsState;

kernel::KernelError TestFs_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kstd::string_view &options) {
  g_testFsState.mountVolumeCalled = true;
  g_testFsState.lastMountedVolume = mount->volume;
  g_testFsState.lastMountOptions = options;
  return kernel::KernelError::Success;
}

kernel::KernelError TestFs_UnmountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount) {
  g_testFsState.unmountVolumeCalled = true;
  g_testFsState.lastUnmountedMount = mount;
  return kernel::KernelError::Success;
}

kernel::KernelError TestFs_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, kernel::KVFSNode &out) {
  out.type = kernel::VFSNodeType::Directory;
  out.size = 0;
  return kernel::KernelError::Success;
}

kernel::KernelError TestFs_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &, kstd::string_view, kernel::KVFSNode &out) {
  out.type = kernel::VFSNodeType::File;
  out.size = 0;
  return kernel::KernelError::Success;
}

kernel::KernelError TestFs_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &, kernel::KIOObject::OpenMode) {
  return g_testFsState.createHandleSucceeds ? kernel::KernelError::Success : kernel::KernelError::AccessDenied;
}

void TestFs_CloseHandle(const kernel::KObjectPtr<kernel::KIOObject> &) {
  ++g_testFsState.closeCount;
}

kstd::expected<uint64_t, kernel::KernelError> TestFs_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &, uint64_t offset, const kstd::span<uint8_t> &buffer) {
  size_t transferred = 0;
  const auto status = KE_IO_ReadDevice(mount->volume, offset, buffer, transferred);
  if (status != kernel::KernelError::Success) return kstd::unexpected(status);
  return transferred;
}

kstd::expected<uint64_t, kernel::KernelError> TestFs_WriteHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mount, const kernel::KVFSNode &, uint64_t offset, const kstd::span<const uint8_t> &buffer) {
  size_t transferred = 0;
  const auto status = KE_IO_WriteDevice(mount->volume, offset, buffer, transferred);
  if (status != kernel::KernelError::Success) return kstd::unexpected(status);
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

    REQUIRE(backing.Resize(imageSize) == kernel::KernelError::Success);

    g_testFsState = TestFsDriverState{};

    driver = kernel::CreateKObject<kernel::KDriverObject>("TestFsDriver").value_or(nullptr);
    driver->Read = diskfake::Read;
    driver->Write = diskfake::Write;

    kstd::static_string<64> volumeName;
    kstd::format(volumeName, "TestFsVolume_{}", imageName);
    volume = KE_IO_CreateDeviceWithContext<diskfake::FakeDiskContext>(driver, volumeName, kernel::device_type::Disk).value_or(nullptr);
    volume->deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

    filesystem = KE_VFS_RegisterFilesystemDriver(driver, fsName).value_or(nullptr);
    if (filesystem) {
      filesystem->MountVolume = TestFs_MountVolume;
      filesystem->UnmountVolume = TestFs_UnmountVolume;
      filesystem->GetRootNode = TestFs_GetRootNode;
      filesystem->Lookup = TestFs_Lookup;
      filesystem->CreateHandle = TestFs_CreateHandle;
      filesystem->CloseHandle = TestFs_CloseHandle;
      filesystem->ReadHandle = TestFs_ReadHandle;
      filesystem->WriteHandle = TestFs_WriteHandle;
    }
  }

  ~TestFsFixture() {
    // The VFS directory is a process-wide singleton, so explicitly detach
    // the mount created by Mount() (if it's still mounted) to avoid leaking
    // it into later, unrelated test cases that mount at the same path. Uses
    // KE_OB_RemoveObject directly rather than KE_VFS_Unmount so tearing down
    // a still-mounted fixture doesn't unexpectedly invoke UnmountVolume.
    if (!mountedAt.empty()) {
      if (const auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), mountedAt)) {
        KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
      }
    }
    std::remove(imagePath.data());
  }

  bool Mount(const kstd::string_view &mountPoint, const kstd::string_view &options) {
    if (KE_VFS_Mount(volume, fsName, mountPoint, options) == kernel::KernelError::Success) {
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
    REQUIRE(KE_OB_FindDirectChildOfType<kernel::KFilesystemObject>(KE_OB_GetFilesystemDirectory(), fixture.fsName).get() == fixture.filesystem.get());
  }

  SECTION("Duplicate filesystem name is rejected") {
    TestFsFixture fixture("register_dup");
    REQUIRE(fixture.filesystem != nullptr);

    const auto duplicate = KE_VFS_RegisterFilesystemDriver(fixture.driver, fixture.fsName).value_or(nullptr);
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

TEST_CASE("KE_VFS_Unmount", "[vfs]") {
  EnsureObjectManagerInitialized();

  SECTION("Unmounting an existing mount then re-mounting at the same path succeeds") {
    TestFsFixture fixture("unmount_remount");
    REQUIRE(fixture.Mount("/", ""));

    REQUIRE(KE_VFS_Unmount("/") == kernel::KernelError::Success);
    // The mount is gone, so a fresh Mount() at the same path must succeed
    // where it would previously have been rejected as "already in use".
    REQUIRE(fixture.Mount("/", ""));
  }

  SECTION("UnmountVolume is invoked with the correct mount object") {
    TestFsFixture fixture("unmount_callback");
    REQUIRE(fixture.Mount("/", ""));

    const auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/");
    REQUIRE(mount != nullptr);

    REQUIRE(KE_VFS_Unmount("/") == kernel::KernelError::Success);
    REQUIRE(g_testFsState.unmountVolumeCalled);
    REQUIRE(g_testFsState.lastUnmountedMount.get() == mount.get());

    // Unmount already dropped the mount; avoid the fixture's destructor
    // trying to remove it a second time.
    fixture.mountedAt.clear();
  }

  SECTION("Unmounting with no UnmountVolume callback still succeeds") {
    TestFsFixture fixture("unmount_nocallback");
    REQUIRE(fixture.filesystem != nullptr);
    fixture.filesystem->UnmountVolume = nullptr;
    REQUIRE(fixture.Mount("/", ""));

    REQUIRE(KE_VFS_Unmount("/") == kernel::KernelError::Success);
    fixture.mountedAt.clear();
  }

  SECTION("Unmounting a non-existent mount point returns false and leaves the VFS directory unchanged") {
    TestFsFixture fixture("unmount_missing");
    REQUIRE(fixture.Mount("/", ""));

    REQUIRE(KE_VFS_Unmount("/nonexistent") != kernel::KernelError::Success);
    // The existing mount at "/" must be untouched.
    REQUIRE(KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/") != nullptr);
  }
}

TEST_CASE("KE_VFS_OpenFile / ReadFile / WriteFile / SeekFile / TellFile / CloseFile round-trip", "[vfs]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("filerw");
  REQUIRE(fixture.Mount("/", ""));

  auto file = KE_VFS_OpenFile("/somefile.txt", kernel::KIOObject::OpenMode::ReadWrite).value_or(nullptr);
  REQUIRE(file != nullptr);
  REQUIRE(file->ops == &kernel::io::FileOps);
  REQUIRE(KE_VFS_TellFile(file).value() == 0);

  kstd::array<uint8_t, 16> writeBuf{};
  for (size_t i = 0; i < writeBuf.size(); i++) writeBuf[i] = static_cast<uint8_t>('A' + i);

  const kstd::span<uint8_t> writeSpan(writeBuf);
  const auto written = KE_VFS_WriteFile(file, writeSpan).value();
  REQUIRE(written == writeBuf.size());
  REQUIRE(KE_VFS_TellFile(file).value() == writeBuf.size());

  REQUIRE(KE_VFS_SeekFile(file, 0).value() == 0);
  REQUIRE(KE_VFS_TellFile(file).value() == 0);

  kstd::array<uint8_t, 16> readBuf{};
  kstd::span<uint8_t> readSpan(readBuf);
  const auto read = KE_VFS_ReadFile(file, readSpan).value();
  REQUIRE(read == readBuf.size());
  REQUIRE(KE_VFS_TellFile(file).value() == readBuf.size());

  for (size_t i = 0; i < readBuf.size(); i++) {
    REQUIRE(readBuf[i] == writeBuf[i]);
  }

  KE_VFS_CloseFile(file);
  KE_VFS_CloseFile(file);
  REQUIRE(g_testFsState.closeCount == 1);
  REQUIRE(file->isClosed);
  const auto closedRead = KE_VFS_ReadFile(file, readSpan);
  REQUIRE_FALSE(closedRead);
  REQUIRE(closedRead.error() == kernel::KernelError::InvalidState);
}

TEST_CASE("KE_VFS_OpenFile cleans up failed opens", "[vfs]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("open_failure");
  REQUIRE(fixture.Mount("/", ""));
  g_testFsState.createHandleSucceeds = false;
  REQUIRE(KE_VFS_OpenFile("/somefile.txt", kernel::KIOObject::OpenMode::Read).value_or(nullptr) == nullptr);
  REQUIRE(g_testFsState.closeCount == 0);
  g_testFsState.createHandleSucceeds = true;
  auto file = KE_VFS_OpenFile("/somefile.txt", kernel::KIOObject::OpenMode::Read).value_or(nullptr);
  REQUIRE(file != nullptr);
  KE_VFS_CloseFile(file);
  REQUIRE(g_testFsState.closeCount == 1);
}

TEST_CASE("KIOObject remains usable across shared handles", "[vfs][io]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("shared_handles");
  REQUIRE(fixture.Mount("/", ""));
  auto file = KE_VFS_OpenFile("/shared.txt", kernel::KIOObject::OpenMode::ReadWrite).value_or(nullptr);
  REQUIRE(file != nullptr);
  kernel::ProcessHandleTable table;
  const auto first = table.Allocate(file, kernel::ProcessHandleTable::HandleAccess::Read,
                                    kernel::ProcessHandleTable::HandleFlags::None).value();
  const auto second = table.Allocate(file, kernel::ProcessHandleTable::HandleAccess::Read,
                                     kernel::ProcessHandleTable::HandleFlags::None).value();
  file = nullptr;
  REQUIRE(table.Close(first) == kernel::KernelError::Success);
  REQUIRE(g_testFsState.closeCount == 0);
  auto remaining = table.Lookup(second)->object.As<kernel::KIOObject>();
  REQUIRE_FALSE(remaining->isClosed);
  uint8_t buffer[1]{};
  size_t transferred = 0;
  REQUIRE(remaining->Read(kstd::span<uint8_t>(buffer, sizeof(buffer)), transferred) == kernel::KernelError::Success);
  REQUIRE(table.Close(second) == kernel::KernelError::Success);
  REQUIRE(g_testFsState.closeCount == 0);
  remaining = nullptr;
  REQUIRE(g_testFsState.closeCount == 1);
}

TEST_CASE("VFS distinguishes EOF and short writes from errors", "[vfs][io][kernel-error]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("expected_io");
  REQUIRE(fixture.Mount("/", ""));
  auto opened = KE_VFS_OpenFile("/counts.txt", kernel::KIOObject::OpenMode::ReadWrite);
  REQUIRE(opened);
  const auto &file = *opened;
  uint8_t buffer[8]{};

  fixture.filesystem->ReadHandle = [](const kernel::KObjectPtr<kernel::KVolumeMountObject> &,
                                     const kernel::KVFSNode &, uint64_t,
                                     const kstd::span<uint8_t> &) -> kstd::expected<uint64_t, kernel::KernelError> {
    return 0ull;
  };
  auto eof = KE_VFS_ReadFile(file, buffer);
  REQUIRE(eof);
  REQUIRE(*eof == 0);
  REQUIRE(file->offset == 0);

  fixture.filesystem->ReadHandle = [](const kernel::KObjectPtr<kernel::KVolumeMountObject> &,
                                     const kernel::KVFSNode &, uint64_t,
                                     const kstd::span<uint8_t> &) -> kstd::expected<uint64_t, kernel::KernelError> {
    return kstd::unexpected(kernel::KernelError::DeviceError);
  };
  auto failed = KE_VFS_ReadFile(file, buffer);
  REQUIRE_FALSE(failed);
  REQUIRE(failed.error() == kernel::KernelError::DeviceError);
  REQUIRE(file->offset == 0);

  fixture.filesystem->WriteHandle = [](const kernel::KObjectPtr<kernel::KVolumeMountObject> &,
                                      const kernel::KVFSNode &, uint64_t,
                                      const kstd::span<const uint8_t> &) -> kstd::expected<uint64_t, kernel::KernelError> {
    return 2ull;
  };
  auto shortWrite = KE_VFS_WriteFile(file, buffer);
  REQUIRE(shortWrite);
  REQUIRE(*shortWrite == 2);
  REQUIRE(file->offset == 2);
}
