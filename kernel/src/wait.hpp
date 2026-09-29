#pragma once

#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/spinlock.hpp>

namespace kernel::wait {

// The lock protecting every dispatcher header's waiter list and the timed
// wait list below. Always take it with SpinLock::lock_irqsave(): the timer
// tick and KE_EVENT_Signal reach the same structures from interrupt
// context. It must never be held across a context switch.
SpinLock &lock() noexcept;

// Intrusive list of wait blocks that carry a deadline. HAL-free so it can be
// exercised in the hosted tests. Callers hold lock() and run with interrupts
// disabled.
struct TimedWaitList {
  KWaitBlock *head{nullptr};

  void Add(KWaitBlock &wb) noexcept {
    wb.timed_next = head;
    head = &wb;
  }

  void Remove(KWaitBlock &wb) noexcept {
    for (KWaitBlock **it = &head; *it; it = &(*it)->timed_next) {
      if (*it == &wb) {
        *it = wb.timed_next;
        wb.timed_next = nullptr;
        return;
      }
    }
  }

  // Flags every unsatisfied block whose deadline is <= now as timed out,
  // unlinks it and calls wake(KThreadObject*).
  template<typename Wake>
  void Expire(uint64_t now_ms, Wake &&wake) noexcept {
    KWaitBlock **it = &head;
    while (KWaitBlock *wb = *it) {
      if (wb->deadline_ms <= now_ms) {
        *it = wb->timed_next;
        wb->timed_next = nullptr;
        if (!wb->satisfied) {
          wb->timed_out = true;
          wake(wb->thread);
        }
      } else {
        it = &wb->timed_next;
      }
    }
  }
};

// Called from the timer interrupt with interrupts disabled. Takes lock().
void tick(uint64_t now_ms) noexcept;

} // namespace kernel::wait
