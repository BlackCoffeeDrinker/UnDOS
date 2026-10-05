#pragma once

#include <stdint.h>

#include <__chrono/duration.hpp>

namespace kernel {
struct KThreadObject;

// One per waiting thread, lives on the waiter's kernel stack (no allocation).
struct KWaitBlock {
  KThreadObject *thread{nullptr};
  KWaitBlock *next{nullptr};      // header waiter list
  KWaitBlock *timed_next{nullptr};// global timed-wait list (wait.cpp)
  kstd::chrono::milliseconds deadline{0};
  bool satisfied{false};
  bool timed_out{false};
};

// Waitable state embedded in events, processes and streams (NT
// DISPATCHER_HEADER pattern). Not synchronised: callers run with interrupts
// disabled.
struct KDispatcherHeader {
  bool signalled{false};
  // Manual-reset stays signalled and releases every waiter; auto-reset
  // releases one waiter and consumes the signal.
  bool manualReset{true};
  KWaitBlock *waiters{nullptr};

  // Consumes the signal if set. Returns true if the wait is satisfied.
  bool TryAcquire() noexcept {
    if (!signalled) return false;
    if (!manualReset) signalled = false;
    return true;
  }

  void AddWaiter(KWaitBlock &wb) noexcept {
    wb.next = nullptr;
    KWaitBlock **tail = &waiters;
    while (*tail) tail = &(*tail)->next;
    *tail = &wb;
  }

  bool RemoveWaiter(KWaitBlock &wb) noexcept {
    for (KWaitBlock **it = &waiters; *it; it = &(*it)->next) {
      if (*it == &wb) {
        *it = wb.next;
        wb.next = nullptr;
        return true;
      }
    }
    return false;
  }

  // Signals the object; calls wake(KThreadObject*) for each released waiter.
  // Returns the number of released waiters.
  template<typename Wake>
  uint32_t Signal(bool wakeAll, Wake &&wake) noexcept {
    uint32_t count = 0;
    if (manualReset || wakeAll) {
      while (KWaitBlock *wb = waiters) {
        waiters = wb->next;
        wb->next = nullptr;
        wb->satisfied = true;
        wake(wb->thread);
        ++count;
      }
      signalled = manualReset;
      return count;
    }
    if (KWaitBlock *wb = waiters) {
      waiters = wb->next;
      wb->next = nullptr;
      wb->satisfied = true;
      wake(wb->thread);
      return 1;
    }
    signalled = true;
    return 0;
  }

  void Reset() noexcept { signalled = false; }
};

} // namespace kernel
