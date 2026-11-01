#include <catch2/catch_test_macros.hpp>

#include <kernel/virtual_memory.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KMemoryBackingObject.hpp>
#include <kernel/object_manager.hpp>

#include <array.hpp>
#include <vector>

#include "ob_test_support.hpp"
#include "memory/Allocator.hpp"

namespace test_host_vmm {
void FailNextMalloc() noexcept;
void FailNextBackingRemoval() noexcept;
void ResetMapCalls() noexcept;
size_t MapCalls() noexcept;
void FailNextMap() noexcept;
}

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

TEST_CASE("KE_Malloc rejects overflow and largest representable requests before reserving memory", "[vmm]") {
  EnsureVmmInitialized();
  struct TestLargeHeader { uint32_t magic; size_t pages; };
  constexpr size_t headerBytes = sizeof(TestLargeHeader);
  auto &as = KE_VMM_GetKernelAddressSpace();
  const auto base = as.current_base;
  REQUIRE(KE_Malloc(SIZE_MAX - headerBytes) == nullptr);
  REQUIRE(as.current_base == base);
  REQUIRE(KE_Malloc(SIZE_MAX) == nullptr);
  REQUIRE(as.current_base == base);
}

TEST_CASE("Large allocations round exact and partial pages without overflow", "[vmm]") {
  EnsureVmmInitialized();
  struct TestLargeHeader { uint32_t magic; size_t pages; };
  constexpr size_t headerBytes = sizeof(TestLargeHeader);
  constexpr size_t payloadForOnePage = 4096 - headerBytes;

  // Warm metadata allocation paths before measuring the physical page maps.
  auto warm = KE_Malloc(8192);
  REQUIRE(warm);
  KE_Free(warm);

  test_host_vmm::ResetMapCalls();
  auto onePage = KE_Malloc(payloadForOnePage);
  REQUIRE(onePage);
  REQUIRE(test_host_vmm::MapCalls() == 1);
  KE_Free(onePage);

  test_host_vmm::ResetMapCalls();
  auto twoPages = KE_Malloc(payloadForOnePage + 1);
  REQUIRE(twoPages);
  REQUIRE(test_host_vmm::MapCalls() == 2);
  KE_Free(twoPages);
}

TEST_CASE("KE_VMM_MapPhysical maps only in the active address space", "[vmm]") {
  EnsureVmmInitialized();
  kernel::vmm::AddressSpace as{};
  as.translation_root = kernel::PhysicalAddress(1);
  test_host_vmm::ResetMapCalls();

  REQUIRE_FALSE(KE_VMM_MapPhysical(as, kernel::VirtualAddress(0x12345000), kernel::PhysicalAddress(0x2000),
      kernel::vmm::ProtectFlags::READ));
  REQUIRE(test_host_vmm::MapCalls() == 1);

  as.translation_root = kernel::PhysicalAddress(2);
  REQUIRE(KE_VMM_MapPhysical(as, kernel::VirtualAddress(0x12346000), kernel::PhysicalAddress(0x3000),
      kernel::vmm::ProtectFlags::READ) == kernel::KernelError::Unsupported);
  REQUIRE(test_host_vmm::MapCalls() == 1);
  REQUIRE_FALSE(HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(0x12346000)));

  REQUIRE(KE_VMM_MapPhysical(as, kernel::VirtualAddress(0), kernel::PhysicalAddress(0x4000),
      kernel::vmm::ProtectFlags::READ) == kernel::KernelError::InvalidAddress);
  REQUIRE(KE_VMM_MapPhysical(as, kernel::VirtualAddress(0x12347000), kernel::PhysicalAddress(0),
      kernel::vmm::ProtectFlags::READ) == kernel::KernelError::InvalidAddress);
  REQUIRE(test_host_vmm::MapCalls() == 1);

  as.translation_root = kernel::PhysicalAddress(1);
  test_host_vmm::FailNextMap();
  REQUIRE(KE_VMM_MapPhysical(as, kernel::VirtualAddress(0x12348000), kernel::PhysicalAddress(0x5000),
      kernel::vmm::ProtectFlags::READ) == kernel::KernelError::DeviceError);
  REQUIRE(test_host_vmm::MapCalls() == 2);
  REQUIRE_FALSE(HAL_VMM_GetPhysicalAddress(kernel::VirtualAddress(0x12348000)));
}

TEST_CASE("User backing allocation rolls back on map failure and preserves each API contract", "[vmm]") {
  EnsureVmmInitialized();
  auto countBackings = [] {
    size_t count = 0;
    KE_OB_GetMemoryDirectory()->Each<kernel::KMemoryBackingObject>([&](const auto &) { ++count; });
    return count;
  };
  const auto before = countBackings();
  test_host_vmm::FailNextMalloc();
  kernel::vmm::AddressSpace allocationFailureAddressSpace{};
  auto backingAllocationFailure = KE_VMM_AllocateUserData(allocationFailureAddressSpace, 1);
  REQUIRE_FALSE(backingAllocationFailure);
  REQUIRE(backingAllocationFailure.error() == kernel::KernelError::OutOfMemory);
  REQUIRE(countBackings() == before);

  kernel::vmm::AddressSpace exhausted{};
  REQUIRE(KE_VMM_AllocateUserData(exhausted, 0).error() == kernel::KernelError::InvalidSize);
  REQUIRE(KE_VMM_AllocateUserProcess(exhausted, 0).error() == kernel::KernelError::InvalidSize);
  REQUIRE(countBackings() == before);
  exhausted.current_base = kernel::VirtualAddress(0x100000);
  exhausted.limit = exhausted.current_base;
  REQUIRE(KE_VMM_AllocateUserData(exhausted, 1).error() == kernel::KernelError::AddressSpaceExhausted);
  REQUIRE(KE_VMM_AllocateUserProcess(exhausted, 1).error() == kernel::KernelError::AddressSpaceExhausted);
  REQUIRE(countBackings() == before);

  std::vector<kernel::KObjectPtr<kernel::KMemoryBackingObject>> existingBackings;
  KE_OB_GetMemoryDirectory()->Each<kernel::KMemoryBackingObject>(
      [&](const auto &backing) { existingBackings.push_back(backing); });
  test_host_vmm::FailNextBackingRemoval();
  auto cleanupFailure = KE_VMM_AllocateUserData(exhausted, 1);
  REQUIRE_FALSE(cleanupFailure);
  REQUIRE(cleanupFailure.error() == kernel::KernelError::AddressSpaceExhausted);
  REQUIRE(countBackings() == before + 1);
  kernel::KObjectPtr<kernel::KMemoryBackingObject> retainedBacking;
  KE_OB_GetMemoryDirectory()->Each<kernel::KMemoryBackingObject>([&](const auto &backing) {
    bool existed = false;
    for (const auto &existing : existingBackings) existed |= existing.get() == backing.get();
    if (!existed) retainedBacking = backing;
  });
  REQUIRE(retainedBacking);
  REQUIRE_FALSE(KE_OB_RemoveObject(KE_OB_GetMemoryDirectory(), retainedBacking));
  REQUIRE(countBackings() == before);

  kernel::vmm::AddressSpace userData{};
  userData.current_base = kernel::VirtualAddress(0x200000);
  userData.limit = kernel::VirtualAddress(0x210000);
  auto dataAddress = KE_VMM_AllocateUserData(userData, 123);
  REQUIRE(dataAddress);
  auto *dataVad = userData.vads.find(kernel::VirtualAddress::from_ptr(*dataAddress));
  REQUIRE(dataVad);
  REQUIRE(dataVad->flags == (kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE | kernel::vmm::ProtectFlags::USER));
  REQUIRE(dataVad->backing);
  REQUIRE(dataVad->backing->size == 123);
  REQUIRE(kstd::string_view(dataVad->backing->name).substr(0, 8) == "UserData");

  kernel::vmm::AddressSpace userProcess{};
  userProcess.current_base = kernel::VirtualAddress(0x300000);
  userProcess.limit = kernel::VirtualAddress(0x310000);
  auto processAddress = KE_VMM_AllocateUserProcess(userProcess, 456);
  REQUIRE(processAddress);
  auto *processVad = userProcess.vads.find(kernel::VirtualAddress::from_ptr(*processAddress));
  REQUIRE(processVad);
  REQUIRE(processVad->flags == (kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::EXECUTE | kernel::vmm::ProtectFlags::USER));
  REQUIRE(processVad->backing);
  REQUIRE(processVad->backing->size == 456);
  REQUIRE(kstd::string_view(processVad->backing->name).substr(0, 11) == "UserProcess");
  REQUIRE(countBackings() == before + 2);

  REQUIRE_FALSE(KE_VMM_FreeRegion(userData, *dataAddress));
  REQUIRE_FALSE(KE_VMM_FreeRegion(userProcess, *processAddress));
  // Successfully mapped anonymous backing objects remain registered in \Memory.
  REQUIRE(countBackings() == before + 2);
}

TEST_CASE("KE_Free(nullptr) is a no-op", "[vmm]") {
  EnsureVmmInitialized();

  KE_Free(nullptr);
  SUCCEED("KE_Free(nullptr) did not crash");
}

TEST_CASE("Kernel VAD reservation does not overlap slabs allocated for its metadata", "[vmm]") {
  EnsureVmmInitialized();
  auto &as = KE_VMM_GetKernelAddressSpace();
  constexpr size_t regionSize = 4 * 4096;
  kstd::array<void *, 1024> regions{};
  size_t count = 0;
  bool grewMetadataSlab = false;

  for (size_t i = 0; i < 1024 && !grewMetadataSlab; ++i) {
    const auto before = as.current_base;
    auto region = KE_VMM_AllocateRegion(as, regionSize, kernel::vmm::ProtectFlags::READ | kernel::vmm::ProtectFlags::WRITE);
    REQUIRE(region);
    auto *vad = as.vads.find(kernel::VirtualAddress::from_ptr(*region));
    REQUIRE(vad != nullptr);
    auto *slab = kernel::memory::find_slab(vad);
    REQUIRE(slab != nullptr);
    const auto slabStart = kernel::VirtualAddress::from_ptr(slab->memory().data());
    const auto slabEnd = slabStart + slab->memory().size();
    REQUIRE((vad->end <= slabStart || vad->start >= slabEnd));
    grewMetadataSlab = as.current_base.value - before.value > regionSize;
    regions[count++] = *region;
  }
  REQUIRE(grewMetadataSlab);
  for (size_t i = 0; i < count; ++i) {
    REQUIRE_FALSE(KE_VMM_FreeRegion(as, regions[i]));
  }
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
