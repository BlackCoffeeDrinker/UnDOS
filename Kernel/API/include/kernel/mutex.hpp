#pragma once

#include <kernel/hal_interface.hpp>
#include <kernel/memory/address.hpp>
#include <kernel/scheduler.hpp>

namespace kernel {

/**
 * @ingroup SYNC
 * @brief A std::mutex-compatible mutual exclusion primitive.
 *
 * Implemented as a spinlock on top of HAL_ATOMIC_CompareExchange32 /
 * HAL_ATOMIC_Store32, yielding the CPU (KE_SCHED_Yield) between attempts so a
 * contended lock does not starve other ready threads. Satisfies the
 * BasicLockable and Lockable named requirements, so it can be used with
 * kstd::lock_guard / kstd::unique_lock (or std::lock_guard / std::unique_lock)
 * exactly like std::mutex.
 */
class Mutex {
 public:
  constexpr Mutex() noexcept = default;

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  
  void lock() noexcept {
    uint32_t expected = kUnlocked;
    while (!HAL_ATOMIC_CompareExchange32(
        VirtualAddress::from_ptr(&state_),
        expected,
        kLocked,
        PlatformMemoryOrder::Acquire)) {
      expected = kUnlocked;
      KE_SCHED_Yield();
    }
  }

  bool try_lock() noexcept {
    uint32_t expected = kUnlocked;
    return HAL_ATOMIC_CompareExchange32(
        VirtualAddress::from_ptr(&state_),
        expected,
        kLocked,
        PlatformMemoryOrder::Acquire);
  }

  void unlock() noexcept {
    HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&state_), kUnlocked, PlatformMemoryOrder::Release);
  }

 private:
  static constexpr uint32_t kUnlocked = 0;
  static constexpr uint32_t kLocked = 1;

  uint32_t state_ = kUnlocked;
};

}// namespace kernel
