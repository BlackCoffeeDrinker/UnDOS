#include <catch2/catch_test_macros.hpp>

#include <kernel/vfs.hpp>

#include <kernel/configuration_manager.hpp>
#include <kernel/io.hpp>
#include <kernel/object_manager.hpp>

#include "ob_test_support.hpp"
#include "stdkrn.hpp"

#include "fake_disk.hpp"
#include "fake_disk_file_backend.hpp"

#include <static_string.hpp>
#include <strfmt.hpp>
#include <cstdio>

// These tests exercise kernel::configuration_manager, in particular:
//  - Registering new hives (KE_CM_RegisterHive) and rejecting duplicates.
//  - KE_CM_OpenRoot lazily formatting a brand-new hive's backing file
//    instead of failing when it has no header yet.
//  - The KConfigurationRoot::configptr_references bookkeeping that keeps the
//    backing file open while at least one KConfigurationKeyPtr referencing
//    the hive is alive, and closes/clears the in-memory index once the last
//    one goes away.
//  - KE_CM_OpenKey correctly reporting a miss on a hive with no keys yet.

namespace {

kstd::static_string<128> MakeTempImagePath(const char *name) {
  kstd::static_string<128> path;
  kstd::format(path, "/tmp/undos_cm_test_{}.img", name);
  return path;
}

// A minimal test filesystem driver, identical in spirit to the one in
// vfs_test.cpp: Lookup always "succeeds" (there is no real notion of
// missing files), and Create/Read/WriteHandle delegate straight to
// KE_IO_ReadDevice/KE_IO_WriteDevice on the mounted fake disk volume.
bool TestFs_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kstd::string_view &) {
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

// Bundles a fresh filesystem driver + fake disk volume mounted at "/", so
// KE_VFS_OpenFile("/whatever", ...) resolves against a private, isolated
// backing store for each test case.
struct TestFsFixture {
  explicit TestFsFixture(const char *imageName, size_t imageSize = 16384)
      : imagePath(MakeTempImagePath(imageName)),
        backing(kstd::string_view(imagePath)) {
    kstd::format(fsName, "CMTestFs_{}", imageName);

    backing.Resize(imageSize);

    driver = kernel::CreateKObject<kernel::KDriverObject>("CMTestFsDriver");
    driver->Read = diskfake::Read;
    driver->Write = diskfake::Write;

    kstd::static_string<64> volumeName;
    kstd::format(volumeName, "CMTestFsVolume_{}", imageName);
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

    mounted = filesystem && KE_VFS_Mount(volume, fsName, "/", "");
  }

  ~TestFsFixture() {
    if (mounted) {
      if (auto mount = KE_OB_FindDirectChildOfType<kernel::KVolumeMountObject>(KE_OB_GetVFSDirectory(), "/")) {
        KE_OB_RemoveObject(KE_OB_GetVFSDirectory(), mount);
      }
    }
    std::remove(imagePath.data());
  }

  bool mounted = false;
  kstd::static_string<64> fsName = "CMTestFs";
  kstd::static_string<128> imagePath;
  diskfake::FileBackingStore backing;
  kernel::KObjectPtr<kernel::KDriverObject> driver;
  kernel::KDevicePtr<kernel::KDevice> volume;
  kernel::KObjectPtr<kernel::KFilesystemObject> filesystem;
};

// Writes a HEADER + a single fully-reserved INDEX TABLE group (170 entries
// + 16-byte next-group pointer, populated only at slot 0) + the payload
// bytes, mirroring configuration_manager.cpp's private on-disk layout, and
// mirrors the same entry into `hive->roots` in memory. Used to seed a value
// for the hive root itself (keyHash 0) so KE_CM_GetKeyValue*/KE_CM_SetKeyValue*
// can be exercised without needing KE_CM_CreateKey (still unimplemented).
void SeedRootValueEntry(const kernel::KObjectPtr<kernel::KConfigurationRoot> &hive,
                        kernel::ConfigurationValueType valueType,
                        const kstd::span<const uint8_t> &data) {
  constexpr uint32_t groupOffset = 8;         // right after the 8-byte header
  constexpr uint32_t indexEntryOffset = groupOffset;// slot 0 of the group
  constexpr uint32_t dataOffset = groupOffset + 4096;// header + one full index group (170*24 + 16)
  const auto length = static_cast<uint16_t>(data.size());

  uint8_t header[8] = {'C', 'F', 'G', '1', 1, 0, 1, 0};// version=1, keyCount=1 (LE)
  KE_VFS_SeekFile(hive->file, 0);
  KE_VFS_WriteFile(hive->file, kstd::span<const uint8_t>(header, sizeof(header)));

  // Zero out the whole reserved index group first (170 empty entries plus a
  // next-group pointer of 0, i.e. no further groups), then overwrite slot 0
  // with our single populated entry.
  uint8_t zeroGroup[4096] = {};
  KE_VFS_SeekFile(hive->file, groupOffset);
  KE_VFS_WriteFile(hive->file, kstd::span<const uint8_t>(zeroGroup, sizeof(zeroGroup)));

  uint8_t entry[24] = {};// keyHash=0 (the hive root itself)
  entry[8] = static_cast<uint8_t>(valueType);
  entry[12] = static_cast<uint8_t>(dataOffset & 0xFF);
  entry[13] = static_cast<uint8_t>((dataOffset >> 8) & 0xFF);
  entry[14] = static_cast<uint8_t>((dataOffset >> 16) & 0xFF);
  entry[15] = static_cast<uint8_t>((dataOffset >> 24) & 0xFF);
  entry[16] = static_cast<uint8_t>(length & 0xFF);
  entry[17] = static_cast<uint8_t>((length >> 8) & 0xFF);
  KE_VFS_SeekFile(hive->file, indexEntryOffset);
  KE_VFS_WriteFile(hive->file, kstd::span<const uint8_t>(entry, sizeof(entry)));

  KE_VFS_SeekFile(hive->file, dataOffset);
  KE_VFS_WriteFile(hive->file, data);

  hive->roots.clear();
  hive->roots.push_back(kernel::KConfigurationRoot::Root{
      .keyHash = 0,
      .valueType = valueType,
      .offset = dataOffset,
      .length = length,
      .indexEntryOffset = indexEntryOffset,
      .groupOffset = groupOffset});
  hive->numRoots = 1;
}

}// namespace

TEST_CASE("KE_CM_RegisterHive registers a new hive under the Hive directory", "[cm]") {
  EnsureObjectManagerInitialized();

  const auto hive = KE_CM_RegisterHive("ConfigTest_Basic", "/ConfigTest_Basic.reg");
  REQUIRE(hive != nullptr);
  REQUIRE(KE_OB_FindDirectChildOfType<kernel::KConfigurationRoot>(KE_OB_GetConfigRoot(), "ConfigTest_Basic").get() == hive.get());
  REQUIRE(kstd::string_view(hive->filePath) == "/ConfigTest_Basic.reg");
  REQUIRE(hive->configptr_references == 0);
  REQUIRE(hive->file == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_RegisterHive rejects a duplicate hive name", "[cm]") {
  EnsureObjectManagerInitialized();

  const auto hive = KE_CM_RegisterHive("ConfigTest_Dup", "/ConfigTest_Dup.reg");
  REQUIRE(hive != nullptr);

  const auto duplicate = KE_CM_RegisterHive("ConfigTest_Dup", "/AnotherPath.reg");
  REQUIRE(duplicate == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_OpenRoot formats a brand-new hive instead of failing", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("open_root_new");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_NewHive", "/newhive.reg");
  REQUIRE(hive != nullptr);

  const auto key = KE_CM_OpenRoot("ConfigTest_NewHive");
  REQUIRE(key != nullptr);
  REQUIRE(hive->file != nullptr);
  REQUIRE(hive->numRoots == 0);
  REQUIRE(hive->configptr_references == 1);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_OpenRoot returns nullptr for an unregistered hive", "[cm]") {
  EnsureObjectManagerInitialized();

  REQUIRE(KE_CM_OpenRoot("ConfigTest_DoesNotExist") == nullptr);
}

TEST_CASE("KConfigurationRoot reference counting keeps the hive open while handles are alive", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("refcount");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_RefCount", "/refcount.reg");
  REQUIRE(hive != nullptr);

  {
    const auto key1 = KE_CM_OpenRoot("ConfigTest_RefCount");
    REQUIRE(key1 != nullptr);
    REQUIRE(hive->configptr_references == 1);
    REQUIRE(hive->file != nullptr);

    {
      const auto key2 = KE_CM_OpenRoot("ConfigTest_RefCount");
      REQUIRE(key2 != nullptr);
      REQUIRE(hive->configptr_references == 2);
    }

    // key2 went out of scope: only key1's reference remains, file stays open.
    REQUIRE(hive->configptr_references == 1);
    REQUIRE(hive->file != nullptr);
  }

  // key1 went out of scope too: the last reference is gone, so the backing
  // file must be closed and the in-memory index released.
  REQUIRE(hive->configptr_references == 0);
  REQUIRE(hive->file == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_OpenKey misses on a freshly formatted, empty hive (fully qualified path)", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("open_key_miss_fq");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_OpenKeyMissFq", "/openkeymissfq.reg");
  REQUIRE(hive != nullptr);

  REQUIRE(KE_CM_OpenKey(nullptr, "ConfigTest_OpenKeyMissFq/SomeKey") == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_OpenKey misses on a freshly formatted, empty hive (relative path via parent key)", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("open_key_miss_rel");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_OpenKeyMissRel", "/openkeymissrel.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_OpenKeyMissRel");
  REQUIRE(root != nullptr);
  REQUIRE(KE_CM_OpenKey(root, "SomeKey") == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_OpenKey rejects a fully qualified path with no root segment", "[cm]") {
  EnsureObjectManagerInitialized();

  REQUIRE(KE_CM_OpenKey(nullptr, "NoSlashInThisName") == nullptr);
}

TEST_CASE("KE_CM_GetKeyValueBool reads back a previously stored bool", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_bool");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueBool", "/getvaluebool.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueBool");
  REQUIRE(root != nullptr);

  const uint8_t raw[1] = {1};
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::Boolean, kstd::span<const uint8_t>(raw, 1));

  bool value = false;
  REQUIRE(KE_CM_GetKeyValueBool(root, value));
  REQUIRE(value == true);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValueUInt32 reads back a previously stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_u32");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueU32", "/getvalueu32.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueU32");
  REQUIRE(root != nullptr);

  const uint32_t expected = 0xDEADBEEF;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::UInt32,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&expected), sizeof(expected)));

  uint32_t value = 0;
  REQUIRE(KE_CM_GetKeyValueUInt32(root, value));
  REQUIRE(value == expected);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValueUInt64 reads back a previously stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_u64");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueU64", "/getvalueu64.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueU64");
  REQUIRE(root != nullptr);

  const uint64_t expected = 0x1122334455667788ull;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::UInt64,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&expected), sizeof(expected)));

  uint64_t value = 0;
  REQUIRE(KE_CM_GetKeyValueUInt64(root, value));
  REQUIRE(value == expected);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValueSInt32 reads back a previously stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_s32");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueS32", "/getvalues32.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueS32");
  REQUIRE(root != nullptr);

  const int32_t expected = -12345;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::SInt32,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&expected), sizeof(expected)));

  int32_t value = 0;
  REQUIRE(KE_CM_GetKeyValueSInt32(root, value));
  REQUIRE(value == expected);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValueSInt64 reads back a previously stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_s64");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueS64", "/getvalues64.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueS64");
  REQUIRE(root != nullptr);

  const int64_t expected = -123456789012345;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::SInt64,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&expected), sizeof(expected)));

  int64_t value = 0;
  REQUIRE(KE_CM_GetKeyValueSInt64(root, value));
  REQUIRE(value == expected);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValueString reads back a previously stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_str");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueStr", "/getvaluestr.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueStr");
  REQUIRE(root != nullptr);

  const kstd::string_view expected = "Hello";
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::String,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(expected.data()), expected.size()));

  kstd::string_view value;
  REQUIRE(KE_CM_GetKeyValueString(root, value));
  REQUIRE(value == expected);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValue* fails on a type mismatch", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_mismatch");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueMismatch", "/getvaluemismatch.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueMismatch");
  REQUIRE(root != nullptr);

  const uint32_t stored = 42;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::UInt32,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored)));

  uint64_t wrongType = 0;
  REQUIRE_FALSE(KE_CM_GetKeyValueUInt64(root, wrongType));

  bool wrongType2 = false;
  REQUIRE_FALSE(KE_CM_GetKeyValueBool(root, wrongType2));

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_GetKeyValue* fails when the key has no stored value", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("get_value_miss");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_GetValueMiss", "/getvaluemiss.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_GetValueMiss");
  REQUIRE(root != nullptr);

  uint32_t value = 0;
  REQUIRE_FALSE(KE_CM_GetKeyValueUInt32(root, value));

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_SetKeyValue* overwrites an existing same-size value in place", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("set_value_inplace");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_SetValueInPlace", "/setvalueinplace.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_SetValueInPlace");
  REQUIRE(root != nullptr);

  const uint32_t initial = 1;
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::UInt32,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&initial), sizeof(initial)));

  REQUIRE(KE_CM_SetKeyValueUInt32(root, 99u));
  REQUIRE(hive->roots.front().offset == 8 + 4096);// no relocation: same size

  uint32_t readBack = 0;
  REQUIRE(KE_CM_GetKeyValueUInt32(root, readBack));
  REQUIRE(readBack == 99u);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_SetKeyValueString relocates the value when it grows", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("set_value_relocate");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_SetValueRelocate", "/setvaluerelocate.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_SetValueRelocate");
  REQUIRE(root != nullptr);

  const kstd::string_view shortValue = "Hi!";
  SeedRootValueEntry(hive, kernel::ConfigurationValueType::String,
                      kstd::span<const uint8_t>(reinterpret_cast<const uint8_t *>(shortValue.data()), shortValue.size()));
  REQUIRE(hive->roots.front().offset == 8 + 4096);

  const kstd::string_view longerValue = "This is a much longer string than before";
  REQUIRE(KE_CM_SetKeyValueString(root, longerValue));
  REQUIRE(hive->roots.front().offset != 8 + 4096);// relocated to the end of the data region
  REQUIRE(hive->roots.front().length == longerValue.size());

  kstd::string_view readBack;
  REQUIRE(KE_CM_GetKeyValueString(root, readBack));
  REQUIRE(readBack == longerValue);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_SetKeyValue* fails when the key has no existing entry", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("set_value_miss");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_SetValueMiss", "/setvaluemiss.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_SetValueMiss");
  REQUIRE(root != nullptr);

  REQUIRE_FALSE(KE_CM_SetKeyValueUInt32(root, 1u));

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_CreateKey adds a new folder-like entry to the index", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("create_key_basic");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_CreateKeyBasic", "/createkeybasic.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_CreateKeyBasic");
  REQUIRE(root != nullptr);
  REQUIRE(hive->numRoots == 0);

  const auto created = KE_CM_CreateKey(root, "NewKey", kernel::KConfigurationKeyAccess::ReadWrite);
  REQUIRE(created != nullptr);
  REQUIRE(hive->numRoots == 1);
  REQUIRE(hive->roots.front().valueType == kernel::ConfigurationValueType::Invalid);
  REQUIRE(hive->roots.front().offset == 0);
  REQUIRE(hive->roots.front().length == 0);

  // The new entry must also be findable through KE_CM_OpenKey().
  const auto reopened = KE_CM_OpenKey(root, "NewKey");
  REQUIRE(reopened != nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_CreateKey returns a handle to an existing entry instead of duplicating it", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("create_key_existing");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_CreateKeyExisting", "/createkeyexisting.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_CreateKeyExisting");
  REQUIRE(root != nullptr);

  REQUIRE(KE_CM_CreateKey(root, "SameKey", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
  REQUIRE(hive->numRoots == 1);

  const auto again = KE_CM_CreateKey(root, "SameKey", kernel::KConfigurationKeyAccess::ReadWrite);
  REQUIRE(again != nullptr);
  REQUIRE(hive->numRoots == 1);// no duplicate entry was added

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_CreateKey with Exclusive access fails if the key already exists", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("create_key_exclusive");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_CreateKeyExclusive", "/createkeyexclusive.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_CreateKeyExclusive");
  REQUIRE(root != nullptr);

  REQUIRE(KE_CM_CreateKey(root, "ExclusiveKey", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
  REQUIRE(hive->numRoots == 1);

  const auto duplicate = KE_CM_CreateKey(root, "ExclusiveKey",
                                         kernel::KConfigurationKeyAccess::ReadWrite | kernel::KConfigurationKeyAccess::Exclusive);
  REQUIRE(duplicate == nullptr);
  REQUIRE(hive->numRoots == 1);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_CreateKey's entry can then be given a value with KE_CM_SetKeyValue*", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("create_key_set_value");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_CreateKeySetValue", "/createkeysetvalue.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_CreateKeySetValue");
  REQUIRE(root != nullptr);

  const auto created = KE_CM_CreateKey(root, "ValueKey", kernel::KConfigurationKeyAccess::ReadWrite);
  REQUIRE(created != nullptr);

  REQUIRE(KE_CM_SetKeyValueUInt32(created, 12345u));

  uint32_t readBack = 0;
  REQUIRE(KE_CM_GetKeyValueUInt32(created, readBack));
  REQUIRE(readBack == 12345u);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_CreateKey allocates a new index group once the current one is full", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("create_key_new_group");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_CreateKeyNewGroup", "/createkeynewgroup.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_CreateKeyNewGroup");
  REQUIRE(root != nullptr);

  const uint32_t firstGroupOffset = 8;// right after the 8-byte header
  kstd::static_string<32> name;
  for (int i = 0; i < 170; i++) {
    kstd::format(name, "Key{}", i);
    REQUIRE(KE_CM_CreateKey(root, kstd::string_view(name), kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
  }
  REQUIRE(hive->numRoots == 170);
  REQUIRE(hive->roots.back().groupOffset == firstGroupOffset);// first group is now completely full

  const auto extraKey = KE_CM_CreateKey(root, "OneMoreKey", kernel::KConfigurationKeyAccess::ReadWrite);
  REQUIRE(extraKey != nullptr);
  REQUIRE(hive->numRoots == 171);
  REQUIRE(hive->roots.back().groupOffset != firstGroupOffset);// landed in a brand-new, chained-in second group

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_DeleteKey removes an existing entry from the index", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("delete_key_basic");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_DeleteKeyBasic", "/deletekeybasic.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_DeleteKeyBasic");
  REQUIRE(root != nullptr);

  REQUIRE(KE_CM_CreateKey(root, "DoomedKey", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
  REQUIRE(hive->numRoots == 1);

  REQUIRE(KE_CM_DeleteKey(root, "DoomedKey"));
  REQUIRE(hive->numRoots == 0);

  // The entry must no longer be discoverable through KE_CM_OpenKey().
  REQUIRE(KE_CM_OpenKey(root, "DoomedKey") == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_DeleteKey fails when the key doesn't exist", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("delete_key_missing");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_DeleteKeyMissing", "/deletekeymissing.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_DeleteKeyMissing");
  REQUIRE(root != nullptr);

  REQUIRE_FALSE(KE_CM_DeleteKey(root, "NoSuchKey"));
  REQUIRE(hive->numRoots == 0);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_DeleteKey persists the deletion across a hive reopen", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("delete_key_reopen");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_DeleteKeyReopen", "/deletekeyreopen.reg");
  REQUIRE(hive != nullptr);

  {
    const auto root = KE_CM_OpenRoot("ConfigTest_DeleteKeyReopen");
    REQUIRE(root != nullptr);

    REQUIRE(KE_CM_CreateKey(root, "KeepMe", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
    REQUIRE(KE_CM_CreateKey(root, "DeleteMe", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
    REQUIRE(hive->numRoots == 2);

    REQUIRE(KE_CM_DeleteKey(root, "DeleteMe"));
    REQUIRE(hive->numRoots == 1);
  }
  // Last reference dropped: the hive's in-memory index is cleared and its
  // file closed, forcing the next open to re-parse from disk.
  REQUIRE(hive->numRoots == 0);

  const auto reopenedRoot = KE_CM_OpenRoot("ConfigTest_DeleteKeyReopen");
  REQUIRE(reopenedRoot != nullptr);
  REQUIRE(hive->numRoots == 1);
  REQUIRE(KE_CM_OpenKey(reopenedRoot, "KeepMe") != nullptr);
  REQUIRE(KE_CM_OpenKey(reopenedRoot, "DeleteMe") == nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_DeleteValue clears a key's value but keeps the key itself", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("delete_value_basic");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_DeleteValueBasic", "/deletevaluebasic.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_DeleteValueBasic");
  REQUIRE(root != nullptr);

  const auto created = KE_CM_CreateKey(root, "ValueHolder", kernel::KConfigurationKeyAccess::ReadWrite);
  REQUIRE(created != nullptr);
  REQUIRE(KE_CM_SetKeyValueUInt32(created, 999u));

  uint32_t readBack = 0;
  REQUIRE(KE_CM_GetKeyValueUInt32(created, readBack));
  REQUIRE(readBack == 999u);

  REQUIRE(KE_CM_DeleteValue(root, "ValueHolder"));
  REQUIRE(hive->numRoots == 1);// the entry itself still exists
  REQUIRE(hive->roots.front().valueType == kernel::ConfigurationValueType::Invalid);

  // The value is gone, but the key can still be opened / re-created.
  REQUIRE_FALSE(KE_CM_GetKeyValueUInt32(created, readBack));
  REQUIRE(KE_CM_OpenKey(root, "ValueHolder") != nullptr);

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}

TEST_CASE("KE_CM_DeleteValue fails when there is no value to delete", "[cm]") {
  EnsureObjectManagerInitialized();
  TestFsFixture fixture("delete_value_missing");
  REQUIRE(fixture.mounted);

  const auto hive = KE_CM_RegisterHive("ConfigTest_DeleteValueMissing", "/deletevaluemissing.reg");
  REQUIRE(hive != nullptr);

  const auto root = KE_CM_OpenRoot("ConfigTest_DeleteValueMissing");
  REQUIRE(root != nullptr);

  // A brand-new key created without a value is Invalid-typed: nothing to delete.
  REQUIRE(KE_CM_CreateKey(root, "EmptyKey", kernel::KConfigurationKeyAccess::ReadWrite) != nullptr);
  REQUIRE_FALSE(KE_CM_DeleteValue(root, "EmptyKey"));

  // A value name that doesn't correspond to any entry at all also fails.
  REQUIRE_FALSE(KE_CM_DeleteValue(root, "NoSuchValue"));

  REQUIRE(KE_OB_RemoveObject(KE_OB_GetConfigRoot(), hive));
}
