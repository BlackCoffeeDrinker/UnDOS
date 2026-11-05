#pragma once

#include <stdint.h>

namespace kernel {
namespace detail {
// Gives up the CPU while a RwMutex is contended (KE_SCHED_Yield). Out of line
// so this header stays free of the scheduler/HAL headers, which themselves
// include virtual_memory.hpp (where AddressSpace embeds a RwMutex).
void rw_mutex_relax() noexcept;
// Atomics routed through HAL_ATOMIC_* (out of line, same reason). Compiler
// __atomic built-ins are not usable: on i386 (no CMPXCHG) GCC lowers them to
// libatomic calls such as __atomic_compare_exchange_4 that nothing provides.
uint32_t rw_load(const uint32_t *word) noexcept;
void rw_store(uint32_t *word, uint32_t value) noexcept;
bool rw_cas(uint32_t *word, uint32_t &expected, uint32_t desired) noexcept;
void rw_add(uint32_t *word, uint32_t delta) noexcept;
}// namespace detail

/**
 * @ingroup SYNC
 * @brief A reader-writer lock, yielding like kernel::Mutex when contended.
 *
 * One 32-bit word: bit 31 = a writer holds the lock, bit 30 = a writer is
 * waiting (new readers back off so writers cannot starve), bits 0..29 =
 * reader count. Uses HAL_ATOMIC_* (via detail::rw_*). Not for interrupt context and
 * not recursive.
 */
class RwMutex {
 public:
  constexpr RwMutex() noexcept = default;

  RwMutex(const RwMutex &) = delete;
  RwMutex &operator=(const RwMutex &) = delete;

  void lock_shared() noexcept {
    while (!try_lock_shared()) detail::rw_mutex_relax();
  }

  bool try_lock_shared() noexcept {
    uint32_t expected = load();
    if (expected & (kWriter | kWriterWaiting)) return false;
    return cas(expected, expected + 1);
  }

  void unlock_shared() noexcept { detail::rw_add(&state_, ~0u); }

  void lock() noexcept {
    for (;;) {
      uint32_t expected = load();
      if ((expected & ~kWriterWaiting) == 0) {
        if (cas(expected, kWriter)) return;
        continue;
      }
      if (!(expected & kWriterWaiting)) cas(expected, expected | kWriterWaiting);
      detail::rw_mutex_relax();
    }
  }

  bool try_lock() noexcept {
    uint32_t expected = load();
    if (expected & ~kWriterWaiting) return false;
    return cas(expected, kWriter);
  }

  // Clears the waiting bit too: other waiting writers set it again.
  void unlock() noexcept { detail::rw_store(&state_, kUnlocked); }

  uint32_t debug_state() const noexcept { return load(); }

 private:
  static constexpr uint32_t kUnlocked = 0;
  static constexpr uint32_t kWriter = 1u << 31;
  static constexpr uint32_t kWriterWaiting = 1u << 30;

  uint32_t load() const noexcept { return detail::rw_load(&state_); }
  bool cas(uint32_t &expected, uint32_t desired) noexcept { return detail::rw_cas(&state_, expected, desired); }

  uint32_t state_ = kUnlocked;
};

// Scoped guards; `engage == false` makes them no-ops (used for the kernel
// address space, which the arena mutex serializes instead).
class SharedGuard {
 public:
  explicit SharedGuard(RwMutex &m, bool engage = true) noexcept : m_(engage ? &m : nullptr) {
    if (m_) m_->lock_shared();
  }
  ~SharedGuard() {
    if (m_) m_->unlock_shared();
  }
  SharedGuard(const SharedGuard &) = delete;
  SharedGuard &operator=(const SharedGuard &) = delete;

 private:
  RwMutex *m_;
};

class ExclusiveGuard {
 public:
  explicit ExclusiveGuard(RwMutex &m, bool engage = true) noexcept : m_(engage ? &m : nullptr) {
    if (m_) m_->lock();
  }
  ~ExclusiveGuard() {
    if (m_) m_->unlock();
  }
  ExclusiveGuard(const ExclusiveGuard &) = delete;
  ExclusiveGuard &operator=(const ExclusiveGuard &) = delete;

 private:
  RwMutex *m_;
};

}// namespace kernel
