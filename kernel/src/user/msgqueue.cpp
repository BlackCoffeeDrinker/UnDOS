#include "user/user.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

namespace kernel::user {
namespace {
MessageQueue *g_queues{nullptr};

// First visible window of this queue with a pending invalid area; WM_PAINT is
// synthesised from it instead of being queued, which coalesces repaints.
KWindowObject *paint_candidate_locked(const MessageQueue &queue) noexcept {
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->queue == &queue && w->Visible() && !w->invalid.Empty()) return w;
  }
  return nullptr;
}

void reset_header(MessageQueue &queue) noexcept {
  HAL_CPU_DisableInterrupts();
  queue.header.Reset();
  HAL_CPU_EnableInterrupts();
}
}// namespace

MessageQueue *queue_for_current_locked(bool create) noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return nullptr;
  for (auto *q = g_queues; q; q = q->next) {
    if (q->thread == self.get()) return q;
  }
  if (!create || !self->owner_process) return nullptr;
  void *p = KE_Malloc(sizeof(MessageQueue));
  if (!p) return nullptr;
  auto *q = new (p) MessageQueue{};
  q->thread = self.get();
  q->process = self->owner_process;
  q->header.manualReset = true;
  q->next = g_queues;
  g_queues = q;
  return q;
}

void wake_locked(MessageQueue &queue) noexcept {
  KE_EVENT_Signal(queue.header, true);
}

bool post_locked(MessageQueue &queue, uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept {
  const QueuedMessage m{hwnd, message, wParam, lParam,
                        static_cast<uint32_t>(KE_TIME_GetSystemTime().count())};
  if (!queue.ring.Push(m)) return false;
  wake_locked(queue);
  return true;
}

bool get_message(QueuedMessage &out, bool block, bool remove) noexcept {
  for (;;) {
    lock();
    MessageQueue *q = queue_for_current_locked(true);
    if (!q) {
      unlock();
      return false;
    }
    if (remove ? q->ring.Pop(out) : q->ring.Peek(out)) {
      unlock();
      return true;
    }
    if (auto *w = paint_candidate_locked(*q)) {
      out = QueuedMessage{w->hwnd, kMsgPaint, 0, 0, static_cast<uint32_t>(KE_TIME_GetSystemTime().count())};
      unlock();
      return true;
    }
    // Nothing pending. Posters signal under the same lock, so resetting here
    // cannot lose a wakeup: a later post sets the header again.
    reset_header(*q);
    unlock();
    if (!block) return false;
    KE_SCHED_WaitForHeader(q->header, kstd::chrono::milliseconds(-1));
  }
}

void free_queues_locked(KProcessObject &proc) noexcept {
  for (MessageQueue **it = &g_queues; *it;) {
    MessageQueue *q = *it;
    if (q->process == &proc) {
      *it = q->next;
      q->~MessageQueue();
      KE_Free(q);
    } else {
      it = &q->next;
    }
  }
}

}// namespace kernel::user
