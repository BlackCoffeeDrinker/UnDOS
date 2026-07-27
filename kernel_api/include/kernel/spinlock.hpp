#pragma once

#include <kernel/hal_interface.hpp>
#include <kernel/memory/address.hpp>

namespace kernel {

/**
 * @ingroup SYNC
 * @brief A busy-wait mutual exclusion primitive safe to use before the
 * scheduler exists and (once IRQ-safe variants are added) from interrupt
 * context.
 *
 * Implemented as a spinlock on top of HAL_ATOMIC_CompareExchange32 /
 * HAL_ATOMIC_Store32 (the same primitives kernel::Mutex uses), but lock()
 * simply busy-waits instead of calling KE_SCHED_Yield(). This makes it usable
 * in places kernel::Mutex is not: before kernel::sched::init() has run, and
 * (later) from ISR/AP-bring-up code. Satisfies the BasicLockable and
 * Lockable named requirements, so it can be used with kstd::lock_guard /
 * kstd::unique_lock exactly like kernel::Mutex.
 *
 * NOTE: lock_irqsave()/unlock_irqrestore() (IRQ-safe variants that save and
 * restore the interrupt-enable state around the critical section) are
 * intentionally NOT provided yet, since no HAL call currently exists to read
 * back the prior interrupt-enable flag. Until that HAL surface exists, do
 * not take a SpinLock from within an ISR.
 */
class SpinLock {
 public:
  constexpr SpinLock() noexcept = default;

  SpinLock(const SpinLock &) = delete;
  SpinLock &operator=(const SpinLock &) = delete;

  void lock() noexcept {
    uint32_t expected = kUnlocked;
    while (!HAL_ATOMIC_CompareExchange32(
        VirtualAddress::from_ptr(&state_),
        expected,
        kLocked,
        PlatformMemoryOrder::Acquire)) {
      expected = kUnlocked;
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
