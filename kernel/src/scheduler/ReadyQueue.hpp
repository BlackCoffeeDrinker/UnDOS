#pragma once

#include <kernel/kobject/KThreadObject.hpp>

namespace kernel::sched {

// HAL-free intrusive ready queue over KThreadObject::rq_next/rq_prev.
//
// Policy: round-robin within a priority level; a larger `priority` value is
// scheduled first. Threads are kept in arrival (FIFO) order, and pop_next
// selects the earliest-arrived thread among those at the highest priority. A
// popped thread that is re-enqueued goes to the back of its level, giving fair
// rotation. This single-list design isolates the policy so it can later be
// replaced by multiple per-priority queues without touching KE_SCHED_* callers.
class ReadyQueue {
  KThreadObject *_head{nullptr};
  KThreadObject *_tail{nullptr};

 public:
  [[nodiscard]] bool empty() const noexcept { return _head == nullptr; }

  // Append `t` at the tail (back of its arrival order).
  void enqueue(KThreadObject *t) noexcept {
    if (!t) return;
    t->rq_next = nullptr;
    t->rq_prev = _tail;
    if (_tail) {
      _tail->rq_next = t;
    } else {
      _head = t;
    }
    _tail = t;
  }

  // Unlink a specific node. Safe for head, middle or tail nodes.
  void remove(KThreadObject *t) noexcept {
    if (!t) return;
    if (t->rq_prev) {
      t->rq_prev->rq_next = t->rq_next;
    } else if (_head == t) {
      _head = t->rq_next;
    }
    if (t->rq_next) {
      t->rq_next->rq_prev = t->rq_prev;
    } else if (_tail == t) {
      _tail = t->rq_prev;
    }
    t->rq_next = nullptr;
    t->rq_prev = nullptr;
  }

  // Return and unlink the highest-priority ready thread (FIFO within a level),
  // or nullptr when the queue is empty.
  KThreadObject *pop_next() noexcept {
    if (!_head) return nullptr;
    KThreadObject *best = _head;
    for (KThreadObject *n = _head->rq_next; n; n = n->rq_next) {
      if (n->priority > best->priority) {
        best = n;
      }
    }
    remove(best);
    return best;
  }
};

} // namespace kernel::sched
