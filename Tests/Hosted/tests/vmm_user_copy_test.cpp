#include <catch2/catch_test_macros.hpp>

#include "object_manager.hpp"
#include "stdkrn.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/virtual_memory.hpp>

#include "task_manager_scheduler_hal_hooks.hpp"

#include <cstdlib>
#include <cstring>

namespace {
using kernel::vmm::ProtectFlags;
using kernel::vmm::VirtualAddressDescriptor;

constexpr size_t kPage = 4096;

// Two user pages backed by host memory, each described by its own VAD, in an
// address space that is the active one (so the direct copy path is taken).
struct UserSpace {
  uint8_t *memory = static_cast<uint8_t *>(std::aligned_alloc(kPage, 2 * kPage));
  kernel::vmm::AddressSpace as{};
  VirtualAddressDescriptor low{};
  VirtualAddressDescriptor high{};

  UserSpace(ProtectFlags lowFlags, ProtectFlags highFlags) {
    std::memset(memory, 0, 2 * kPage);
    test_sched_hal::SetRoot(kernel::PhysicalAddress(0x7000));
    as.translation_root = kernel::PhysicalAddress(0x7000);
    const auto base = kernel::VirtualAddress::from_ptr(memory);
    low.start = base;
    low.end = base + kPage;
    low.flags = lowFlags;
    high.start = base + kPage;
    high.end = base + 2 * kPage;
    high.flags = highFlags;
    as.vads.insert(low);
    as.vads.insert(high);
    REQUIRE(HAL_VMM_MapPage(low.start, kernel::PhysicalAddress(0x100000), lowFlags));
    REQUIRE(HAL_VMM_MapPage(high.start, kernel::PhysicalAddress(0x101000), highFlags));
  }

  ~UserSpace() {
    HAL_VMM_UnmapPage(low.start);
    HAL_VMM_UnmapPage(high.start);
    as.vads.remove(&low);
    as.vads.remove(&high);
    test_sched_hal::Reset();
    std::free(memory);
  }
};

constexpr auto kUserRW = ProtectFlags::READ | ProtectFlags::WRITE | ProtectFlags::USER;
constexpr auto kUserRO = ProtectFlags::READ | ProtectFlags::USER;
}// namespace

TEST_CASE("KE_VMM_CopyFromUser/CopyToUser copy directly across VADs", "[vmm][usercopy]") {
  UserSpace user(kUserRW, kUserRW);
  uint8_t out[64];
  for (size_t i = 0; i < sizeof(out); ++i) out[i] = static_cast<uint8_t>(i + 1);

  // Straddles the boundary between the two VADs / pages.
  uint8_t *target = user.memory + kPage - 16;
  REQUIRE_FALSE(KE_VMM_CopyToUser(user.as, target, out, sizeof(out)));
  REQUIRE(std::memcmp(target, out, sizeof(out)) == 0);

  uint8_t in[64]{};
  REQUIRE_FALSE(KE_VMM_CopyFromUser(user.as, in, target, sizeof(in)));
  REQUIRE(std::memcmp(in, out, sizeof(in)) == 0);
  REQUIRE_FALSE(KE_VMM_CopyFromUser(user.as, in, target, 0));
}

TEST_CASE("KE_VMM_CopyToUser refuses read-only user memory", "[vmm][usercopy]") {
  UserSpace user(kUserRW, kUserRO);
  const uint8_t value[32]{0xAA};
  // The write crosses into the read-only page: it fails there, before any
  // byte of that page is written.
  REQUIRE(KE_VMM_CopyToUser(user.as, user.memory + kPage - 8, value, sizeof(value)));
  REQUIRE(user.memory[kPage] == 0);
  // Reading it is fine.
  uint8_t in[8];
  REQUIRE_FALSE(KE_VMM_CopyFromUser(user.as, in, user.memory + kPage, sizeof(in)));
}

TEST_CASE("RwMutex shares readers and excludes writers", "[sync][rwmutex]") {
  kernel::RwMutex m;
  REQUIRE(m.try_lock_shared());
  REQUIRE(m.try_lock_shared());
  REQUIRE_FALSE(m.try_lock());
  m.unlock_shared();
  m.unlock_shared();
  REQUIRE(m.try_lock());
  REQUIRE_FALSE(m.try_lock_shared());
  REQUIRE_FALSE(m.try_lock());
  m.unlock();
  REQUIRE(m.debug_state() == 0);
  {
    kernel::SharedGuard a(m);
    kernel::SharedGuard b(m);
    REQUIRE_FALSE(m.try_lock());
  }
  {
    kernel::ExclusiveGuard w(m);
    REQUIRE_FALSE(m.try_lock_shared());
  }
  kernel::ExclusiveGuard off(m, false);
  REQUIRE(m.debug_state() == 0);
}

TEST_CASE("RwMutex: a waiting writer blocks new readers", "[sync][rwmutex]") {
  kernel::RwMutex m;
  REQUIRE(m.try_lock_shared());
  // Simulate a writer that found the lock busy and announced itself.
  __atomic_fetch_or(reinterpret_cast<uint32_t *>(&m), 1u << 30, __ATOMIC_RELAXED);
  REQUIRE_FALSE(m.try_lock_shared());
  REQUIRE_FALSE(m.try_lock());
  m.unlock_shared();
  REQUIRE(m.try_lock());
  m.unlock();
  REQUIRE(m.debug_state() == 0);
}

TEST_CASE("User copies leave the address-space lock released", "[vmm][usercopy][rwmutex]") {
  UserSpace user(kUserRW, kUserRO);
  uint8_t buf[16]{};
  REQUIRE_FALSE(KE_VMM_CopyToUser(user.as, user.memory, buf, sizeof(buf)));
  REQUIRE(KE_VMM_CopyToUser(user.as, user.memory + kPage, buf, sizeof(buf)));
  REQUIRE(user.as.lock.debug_state() == 0);
}

TEST_CASE("KE_VMM_CopyFromUser refuses kernel-only, unmapped and wrapping ranges", "[vmm][usercopy]") {
  UserSpace user(ProtectFlags::READ | ProtectFlags::WRITE, kUserRW);
  uint8_t in[8];
  REQUIRE(KE_VMM_CopyFromUser(user.as, in, user.memory, sizeof(in)));
  REQUIRE(KE_VMM_CopyFromUser(user.as, in, nullptr, sizeof(in)));
  REQUIRE(KE_VMM_CopyFromUser(user.as, in, reinterpret_cast<void *>(UINTPTR_MAX - 2), sizeof(in)));

  HAL_VMM_UnmapPage(user.high.start);
  REQUIRE(KE_VMM_CopyFromUser(user.as, in, user.memory + kPage, sizeof(in)));
}
