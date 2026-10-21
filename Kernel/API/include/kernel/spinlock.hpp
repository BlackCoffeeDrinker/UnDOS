#pragma once

#include <kernel/hal_interface.hpp>
#include <kernel/memory/address.hpp>

namespace kernel {

/**
 * @ingroup SYNC
 * @brief A busy-wait mutual exclusion primitive safe to use before the
 * scheduler exists and, via lock_irqsave(), from interrupt context.
 *
 * Implemented as a spinlock on top of HAL_ATOMIC_CompareExchange32 /
 * HAL_ATOMIC_Store32 (the same primitives kernel::Mutex uses), but lock()
 * simply busy-waits instead of calling KE_SCHED_Yield(). This makes it usable
 * in places kernel::Mutex is not: before the scheduler has been brought up,
 * and from ISR/AP-bring-up code. Satisfies the BasicLockable and
 * Lockable named requirements, so it can be used with kstd::lock_guard /
 * kstd::unique_lock exactly like kernel::Mutex.
 *
 * lock_irqsave()/unlock_irqrestore() additionally mask interrupts on the
 * local CPU for the duration of the critical section, so a lock that is also
 * taken from an ISR cannot deadlock against that ISR firing on the CPU that
 * already holds it. They nest: the token returned by lock_irqsave() carries
 * the prior interrupt-enable state, and unlock_irqrestore() restores exactly
 * that, leaving interrupts disabled if they were already disabled.
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

  /**
   * @brief Masks interrupts on this CPU, then takes the lock.
   * @return An opaque token to hand back to unlock_irqrestore().
   */
  [[nodiscard]] uintptr_t lock_irqsave() noexcept {
    const uintptr_t state = HAL_CPU_SaveAndDisableInterrupts();
    lock();
    return state;
  }

  /**
   * @brief Releases the lock, then restores the interrupt-enable state the
   * matching lock_irqsave() captured.
   */
  void unlock_irqrestore(const uintptr_t state) noexcept {
    unlock();
    HAL_CPU_RestoreInterrupts(state);
  }

 private:
  static constexpr uint32_t kUnlocked = 0;
  static constexpr uint32_t kLocked = 1;

  uint32_t state_ = kUnlocked;
};

/**
 * @ingroup SYNC
 * @brief RAII wrapper around SpinLock::lock_irqsave() /
 * SpinLock::unlock_irqrestore().
 */
class IrqSpinLockGuard {
 public:
  explicit IrqSpinLockGuard(SpinLock &lock) noexcept
      : lock_(lock), state_(lock.lock_irqsave()) {}

  ~IrqSpinLockGuard() noexcept { lock_.unlock_irqrestore(state_); }

  IrqSpinLockGuard(const IrqSpinLockGuard &) = delete;
  IrqSpinLockGuard &operator=(const IrqSpinLockGuard &) = delete;

 private:
  SpinLock &lock_;
  uintptr_t state_;
};

}// namespace kernel
