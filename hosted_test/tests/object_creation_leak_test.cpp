#include <catch2/catch_test_macros.hpp>

#include "stdkrn.hpp"

#include <kernel/configuration_manager.hpp>
#include <kernel/kobject/KDirectoryObject.hpp>
#include <kernel/object_manager.hpp>

#include "object_manager.hpp"
#include "vmm.hpp"

#include "ob_test_support.hpp"

// These tests prove that KE_CreateObject, KE_CreateKPtrObject and
// kernel::CreateKObject actually return their memory to the allocator (via
// KE_Free) once the returned smart pointer's last reference goes away,
// instead of leaking it.
//
// The proof relies on kernel::vmm::debug_slab_cache_allocated_bytes(), which
// reports how many bytes are currently outstanding (allocated but not yet
// freed) in the fixed-size slab cache that would service a KE_Malloc(size)
// request for a given object. KE_Malloc/KE_Free are backed by the real slab
// allocator (kernel/src/memory/*.cpp) in this host build, so a genuine leak
// would leave this counter permanently elevated after the object's owning
// smart pointer has been destroyed, while a correctly freed object always
// brings it back down to its pre-test baseline.

namespace {

struct LeakTestPod {
  uint32_t value{0};
};

}// namespace

TEST_CASE("KE_CreateObject frees its memory back to the allocator", "[leak]") {
  EnsureObjectManagerInitialized();

  const size_t baseline = kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(LeakTestPod));

  {
    auto obj = KE_CreateObject<LeakTestPod>();
    REQUIRE(obj);
    obj.value()->value = 42;
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(LeakTestPod)) > baseline);
    // obj destructs here, KMallocDeleter must call KE_Free on it.
  }

  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(LeakTestPod)) == baseline);

  // Repeat creation/destruction a few times: the outstanding byte count must
  // always return to the same baseline, never accumulate.
  for (int i = 0; i < 8; ++i) {
    auto obj = KE_CreateObject<LeakTestPod>();
    REQUIRE(obj);
  }
  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(LeakTestPod)) == baseline);
}

TEST_CASE("KE_CreateKPtrObject (KObject-derived) frees its memory back to the allocator", "[leak]") {
  EnsureObjectManagerInitialized();

  const size_t baseline = kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject));

  {
    auto dir = KE_CreateKPtrObject<kernel::KDirectoryObject>();
    REQUIRE(dir);
    REQUIRE(dir.value()->reference_count.load() == 1);
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) > baseline);
    // dir destructs here: reference_count reaches 0 and KE_Free must run.
  }

  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) == baseline);

  for (int i = 0; i < 8; ++i) {
    auto dir = KE_CreateKPtrObject<kernel::KDirectoryObject>();
    REQUIRE(dir);
  }
  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) == baseline);
}

TEST_CASE("KE_CreateKPtrObject (plain handle, KConfigurationKey) frees its memory back to the allocator", "[leak]") {
  EnsureObjectManagerInitialized();

  const size_t baseline = kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KConfigurationKey));

  {
    auto key = KE_CreateKPtrObject<kernel::KConfigurationKey>();
    REQUIRE(key);
    REQUIRE(key.value()->handle_references.load() == 1);
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KConfigurationKey)) > baseline);
    // key destructs here: handle_references reaches 0 and KE_Free must run.
  }

  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KConfigurationKey)) == baseline);

  for (int i = 0; i < 8; ++i) {
    auto key = KE_CreateKPtrObject<kernel::KConfigurationKey>();
    REQUIRE(key);
  }
  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KConfigurationKey)) == baseline);
}

TEST_CASE("kernel::CreateKObject frees its memory back to the allocator", "[leak]") {
  EnsureObjectManagerInitialized();

  const size_t baseline = kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject));

  {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("LeakTest_CreateKObject");
    REQUIRE(dir);
    REQUIRE(dir.value()->reference_count.load() == 1);
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) > baseline);
    // dir destructs here: reference_count reaches 0 and KE_Free must run.
  }

  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) == baseline);

  for (int i = 0; i < 8; ++i) {
    auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("LeakTest_CreateKObject");
    REQUIRE(dir);
  }
  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) == baseline);
}

TEST_CASE("KE_CreateKPtrObject/CreateKObject don't leak when inserted into and removed from the object tree", "[leak]") {
  EnsureObjectManagerInitialized();
  const auto device = KE_OB_GetDeviceDirectory();

  const size_t baseline = kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject));

  {
    const auto dir = kernel::CreateKObject<kernel::KDirectoryObject>("LeakTest_InsertRemove");

    REQUIRE(dir);
    REQUIRE(KE_OB_InsertObject(device, *dir) == kernel::KernelError::Success);
    REQUIRE(dir.value()->reference_count.load() == 2);

    REQUIRE(KE_OB_RemoveObject(device, *dir) == kernel::KernelError::Success);
    REQUIRE(dir.value()->reference_count.load() == 1);
    // dir (the last remaining reference) destructs here.
  }

  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(sizeof(kernel::KDirectoryObject)) == baseline);
}
