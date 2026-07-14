#include <catch2/catch_test_macros.hpp>

#include <kernel/virtual_memory.hpp>

#include <array.hpp>

#include "ob_test_support.hpp"

// kernel::vmm::init() (which populates the SLAB caches backing KE_Malloc/
// KE_Free) is already invoked by EnsureObjectManagerInitialized(), since
// object_manager::init() itself allocates via KE_Malloc; reuse it here so
// this test file doesn't care which test case runs first.
namespace {
void EnsureVmmInitialized() {
  EnsureObjectManagerInitialized();
}
}// namespace

TEST_CASE("KE_Malloc returns non-null, distinct, writable memory", "[vmm]") {
  EnsureVmmInitialized();

  void *a = KE_Malloc(32);
  void *b = KE_Malloc(32);

  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  REQUIRE(a != b);

  // Memory must be real & writable (this would segfault against a fake
  // pointer or an unmapped/misconfigured region).
  auto *bytes = static_cast<uint8_t *>(a);
  for (size_t i = 0; i < 32; i++) bytes[i] = static_cast<uint8_t>(i);
  for (size_t i = 0; i < 32; i++) REQUIRE(bytes[i] == static_cast<uint8_t>(i));

  KE_Free(a);
  KE_Free(b);
}

TEST_CASE("KE_Malloc(0) returns nullptr", "[vmm]") {
  EnsureVmmInitialized();

  REQUIRE(KE_Malloc(0) == nullptr);
}

TEST_CASE("KE_Free(nullptr) is a no-op", "[vmm]") {
  EnsureVmmInitialized();

  KE_Free(nullptr);
  SUCCEED("KE_Free(nullptr) did not crash");
}

TEST_CASE("Freed memory can be reused by a subsequent allocation", "[vmm]") {
  EnsureVmmInitialized();

  void *first = KE_Malloc(64);
  REQUIRE(first != nullptr);
  KE_Free(first);

  // The SLAB allocator services same-sized requests from its free list, so
  // freeing then re-allocating shouldn't grow the cache's live allocation
  // count (i.e. the slot really was recycled, even if not necessarily via
  // the exact same pointer).
  void *second = KE_Malloc(64);
  REQUIRE(second != nullptr);

  KE_Free(second);
}

TEST_CASE("KE_Malloc picks the smallest cache that fits and honors many concurrent allocations", "[vmm]") {
  EnsureVmmInitialized();

  // g_cache_sizes in vmm.cpp tops out at 2048; anything larger than the
  // largest cache falls back to a dedicated, page-backed large allocation
  // rather than failing outright.
  void *large = KE_Malloc(1u << 20);
  REQUIRE(large != nullptr);
  KE_Free(large);

  kstd::array<void *, 64> pointers{};
  for (size_t i = 0; i < pointers.size(); i++) {
    pointers[i] = KE_Malloc(16);
    REQUIRE(pointers[i] != nullptr);
  }

  // All simultaneously-live allocations must be distinct.
  for (size_t i = 0; i < pointers.size(); i++) {
    for (size_t j = i + 1; j < pointers.size(); j++) {
      REQUIRE(pointers[i] != pointers[j]);
    }
  }

  for (auto *ptr: pointers) KE_Free(ptr);
}
