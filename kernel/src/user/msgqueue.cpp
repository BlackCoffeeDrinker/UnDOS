#include "user/user.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

namespace kernel::user {
namespace {
MessageQueue *g_queues{nullptr};

// First visible window of this queue with a pending invalid area; WM_PAINT is
// synthesised from it instead of being queued, which coalesces repaints.
KWindowObject *paint_candidate_locked(const MessageQueue &queue) noexcept {
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->paintDC && w->process && !w->process->dcTable.Resolve(w->paintDC)) {
      w->invalid = w->invalid.Union(w->painting);
      w->paintDC = 0;
      w->painting = {};
      w->paintClip.Clear();
    }
    if (w->queue == &queue && effectively_visible_locked(*w) && !w->paintDC && !w->invalid.Empty()) return w;
  }
  return nullptr;
}

void reset_header(MessageQueue &queue) noexcept {
  const uintptr_t irq_state = HAL_CPU_SaveAndDisableInterrupts();
  queue.header.Reset();
  HAL_CPU_RestoreInterrupts(irq_state);
}
}// namespace

kstd::expected<MessageQueue *, KernelError> queue_for_current_locked(bool create) noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return kstd::unexpected(KernelError::NoCurrentThread);
  for (auto *q = g_queues; q; q = q->next) {
    if (q->thread == self.get()) return q;
  }
  if (!create) return kstd::unexpected(KernelError::NotFound);
  if (!self->owner_process) return kstd::unexpected(KernelError::NoCurrentProcess);
  void *p = KE_Malloc(sizeof(MessageQueue));
  if (!p) return kstd::unexpected(KernelError::OutOfMemory);
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

KernelError post_locked(MessageQueue &queue, uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept {
  const QueuedMessage m{.hwnd = hwnd, .message = message, .wParam = wParam, .lParam = lParam,
                        .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
  if (const KernelError pushed = queue.ring.Push(m); pushed != KernelError::Success) return pushed;
  wake_locked(queue);
  return KernelError::Success;
}

KernelError post_message(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept {
  Guard guard;
  if (hwnd == 0) {
    const auto queue = queue_for_current_locked(true);
    if (!queue) return queue.error();
    return post_locked(**queue, 0, message, wParam, lParam);
  }
  auto *w = lookup_locked(hwnd);
  if (!w) return KernelError::InvalidHandle;
  if (!w->queue) return KernelError::InvalidState;
  return post_locked(*w->queue, hwnd, message, wParam, lParam);
}

bool geometry_message_locked(const MessageQueue &queue, QueuedMessage &out, bool remove) noexcept {
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->queue != &queue) continue;
    if (!w->pendingGeometry && w->pendingDisplayChange) {
      out = QueuedMessage{.hwnd = w->hwnd, .message = kMsgDisplayChange, .wParam = w->displayBpp, .lParam = w->displaySize,
                          .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
      if (remove) w->pendingDisplayChange = false;
      return true;
    }
    if (!w->pendingGeometry) continue;
    const bool move = (w->pendingGeometry & 1) != 0;
    out = QueuedMessage{.hwnd = w->hwnd, .message = move ? kMsgMove : kMsgSize, .wParam = move ? 0 : static_cast<uint32_t>(w->state),
                        .lParam = move ? w->moveParam : w->sizeParam,
                        .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
    if (remove) w->pendingGeometry &= static_cast<uint8_t>(move ? ~1u : ~2u);
    return true;
  }
  return false;
}

kstd::expected<QueuedMessage, KernelError> get_message(bool block, bool remove) noexcept {
  for (;;) {
    lock();
    const auto queue = queue_for_current_locked(true);
    if (!queue) {
      unlock();
      return kstd::unexpected(queue.error());
    }
    MessageQueue *q = *queue;
    QueuedMessage out{};
    if (remove ? q->ring.Pop(out) : q->ring.Peek(out)) {
      unlock();
      return out;
    }
    if (geometry_message_locked(*q, out, remove)) {
      unlock();
      return out;
    }
    if (const auto *w = paint_candidate_locked(*q)) {
      out = QueuedMessage{.hwnd = w->hwnd, .message = kMsgPaint, .wParam = 0, .lParam = 0, .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
      unlock();
      return out;
    }
    // Nothing pending. Posters signal under the same lock, so resetting here
    // cannot lose a wakeup: a later post sets the header again.
    reset_header(*q);
    unlock();
    if (!block) return kstd::unexpected(KernelError::WouldBlock);
    if (const KernelError waited = KE_SCHED_WaitForHeader(q->header, kstd::chrono::milliseconds(-1));
        waited != KernelError::Success) return kstd::unexpected(waited);
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
