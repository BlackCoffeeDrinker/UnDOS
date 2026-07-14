#include <catch2/catch_test_macros.hpp>

#include <kernel/object_manager.hpp>
#include <kernel/kobject/KDirectoryObject.hpp>

#include "object_manager.hpp"
#include "stdkrn.hpp"

#include "ob_test_support.hpp"

TEST_CASE("Object Manager well-known directories are non-null and distinct", "[ob]") {
  EnsureObjectManagerInitialized();

  const auto root = KE_OB_GetRootDirectory();
  const auto device = KE_OB_GetDeviceDirectory();
  const auto driver = KE_OB_GetDriverDirectory();
  const auto memory = KE_OB_GetMemoryDirectory();
  const auto thread = KE_OB_GetThreadsDirectory();
  const auto process = KE_OB_GetProcessDirectory();
  const auto vfs = KE_OB_GetVFSDirectory();
  const auto file = KE_OB_GetFileRoot();
  const auto filesystem = KE_OB_GetFilesystemRoot();

  REQUIRE(root != nullptr);
  REQUIRE(device != nullptr);
  REQUIRE(driver != nullptr);
  REQUIRE(memory != nullptr);
  REQUIRE(thread != nullptr);
  REQUIRE(process != nullptr);
  REQUIRE(vfs != nullptr);
  REQUIRE(file != nullptr);
  REQUIRE(filesystem != nullptr);

  REQUIRE(device.get() != driver.get());
  REQUIRE(device.get() != vfs.get());
  REQUIRE(vfs.get() != file.get());
  REQUIRE(filesystem.get() != file.get());

  REQUIRE(KE_OB_LookupObject("/VFS").get() == vfs.get());
  REQUIRE(KE_OB_LookupObject("/Device").get() == device.get());
}

TEST_CASE("KE_OB_InsertObject / KE_OB_RemoveObject", "[ob]") {
  EnsureObjectManagerInitialized();
  const auto device = KE_OB_GetDeviceDirectory();

  SECTION("Successful insert makes the object findable and retains it") {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_InsertOk");
    const auto *rawPtr = dir.get();

    REQUIRE(KE_OB_InsertObject(device, dir));
    REQUIRE(dir->parent == device.get());
    REQUIRE(KE_OB_FindDirectChild(device, "ObTest_InsertOk").get() == rawPtr);

    REQUIRE(KE_OB_RemoveObject(device, dir));
  }

  SECTION("Duplicate name insert is rejected") {
    auto dir1 = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_Dup");
    auto dir2 = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_Dup");

    REQUIRE(KE_OB_InsertObject(device, dir1));
    REQUIRE_FALSE(KE_OB_InsertObject(device, dir2));
    REQUIRE(dir2->parent == nullptr);

    REQUIRE(KE_OB_RemoveObject(device, dir1));
  }

  SECTION("Inserting an object that already has a parent is rejected") {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_AlreadyParented");
    REQUIRE(KE_OB_InsertObject(device, dir));

    const auto driver = KE_OB_GetDriverDirectory();
    REQUIRE_FALSE(KE_OB_InsertObject(driver, dir));
    REQUIRE(dir->parent == device.get());

    REQUIRE(KE_OB_RemoveObject(device, dir));
  }

  SECTION("Reference count changes across insert/remove") {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_Refcount");
    REQUIRE(dir->reference_count.load() == 1);

    REQUIRE(KE_OB_InsertObject(device, dir));
    REQUIRE(dir->reference_count.load() == 2);

    REQUIRE(KE_OB_RemoveObject(device, dir));
    REQUIRE(dir->reference_count.load() == 1);
  }

  SECTION("Removing with a mismatched parent fails and leaves the object attached") {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_WrongParent");
    REQUIRE(KE_OB_InsertObject(device, dir));

    const auto driver = KE_OB_GetDriverDirectory();
    REQUIRE_FALSE(KE_OB_RemoveObject(driver, dir));
    REQUIRE(dir->parent == device.get());

    REQUIRE(KE_OB_RemoveObject(device, dir));
  }
}

TEST_CASE("KE_OB_LookupObject / KE_OB_LookupObjectWithRoot / KE_OB_FindDirectChild", "[ob]") {
  EnsureObjectManagerInitialized();
  const auto device = KE_OB_GetDeviceDirectory();

  auto level1 = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_Level1");
  auto level2 = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_Level2");
  const auto *level2Ptr = level2.get();

  REQUIRE(KE_OB_InsertObject(device, level1));
  REQUIRE(KE_OB_InsertObject(level1, level2));

  SECTION("Multi-level absolute path lookup") {
    const auto found = KE_OB_LookupObject("/Device/ObTest_Level1/ObTest_Level2");
    REQUIRE(found.get() == level2Ptr);
  }

  SECTION("Missing segment returns null") {
    REQUIRE(KE_OB_LookupObject("/Device/ObTest_Level1/DoesNotExist") == nullptr);
    REQUIRE(KE_OB_LookupObject("/Device/DoesNotExist/ObTest_Level2") == nullptr);
  }

  SECTION("'.' resolves to the same object, '..' resolves to its parent") {
    const auto asDot = KE_OB_LookupObjectWithRoot(level1, ".");
    REQUIRE(asDot.get() == level1.get());

    const auto asDotDot = KE_OB_LookupObjectWithRoot(level2, "..");
    REQUIRE(asDotDot.get() == level1.get());
  }

  SECTION("KE_OB_FindDirectChild finds only direct children") {
    REQUIRE(KE_OB_FindDirectChild(device, "ObTest_Level1").get() == level1.get());
    REQUIRE(KE_OB_FindDirectChild(device, "ObTest_Level2") == nullptr);
    REQUIRE(KE_OB_FindDirectChild(level1, "ObTest_Level2").get() == level2Ptr);
  }

  SECTION("KE_OB_LookupObjectWithRoot resolves a relative path from a given root") {
    const auto found = KE_OB_LookupObjectWithRoot(level1, "ObTest_Level2");
    REQUIRE(found.get() == level2Ptr);
  }

  REQUIRE(KE_OB_RemoveObject(level1, level2));
  REQUIRE(KE_OB_RemoveObject(device, level1));
}

TEST_CASE("KE_OB_FindDirectChildOfType filters by object type", "[ob]") {
  EnsureObjectManagerInitialized();
  const auto device = KE_OB_GetDeviceDirectory();

  auto subDir = kernel::CreateKObject<kernel::KDirectoryObject>("ObTest_TypedChild");
  REQUIRE(KE_OB_InsertObject(device, subDir));

  SECTION("Matching type returns the object") {
    const auto found = KE_OB_FindDirectChildOfType<kernel::KDirectoryObject>(device, "ObTest_TypedChild");
    REQUIRE(found.get() == subDir.get());
  }

  SECTION("Missing child returns null") {
    const auto found = KE_OB_FindDirectChildOfType<kernel::KDirectoryObject>(device, "ObTest_DoesNotExist");
    REQUIRE(found == nullptr);
  }

  REQUIRE(KE_OB_RemoveObject(device, subDir));
}
