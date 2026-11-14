#include "user/user.hpp"

#include <Kernel.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

namespace kernel::user {
namespace {
// Message waits are alertable: a process exit ends them so the thread retires.
kstd::error_code alertable_wait(KDispatcherHeader &header, kstd::chrono::milliseconds timeout) noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (self) self->alertable = true;
  const auto status = KE_SCHED_WaitForHeader(header, timeout);
  if (self) self->alertable = false;
  return status;
}
MessageQueue *g_queues{nullptr};

// First visible window of this queue with a pending invalid area; WM_PAINT is
// synthesised from it instead of being queued, which coalesces repaints.
KWindowObject *paint_candidate_locked(const MessageQueue &queue) noexcept {
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->paintDC && w->process && !kernel::gfx::FindDC(w->process, w->paintDC))
      abandon_paint_locked(*w);
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

bool should_serve_paint_before_input_locked(const MessageQueue &queue) noexcept {
  return queue.ring.count >= kPaintFairnessBacklog &&
         paint_candidate_locked(queue) != nullptr;
}

kstd::expected<MessageQueue *, kstd::error_code> queue_for_current_locked() noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return kstd::unexpected(KernelError::NoCurrentThread);
  for (auto *q = g_queues; q; q = q->next) {
    if (q->thread == self.get()) return q;
  }
  return kstd::unexpected(KernelError::NotFound);
}

kstd::error_code ensure_current_queue() noexcept {
  const auto self = KE_SCHED_GetCurrentThread();
  if (!self) return KernelError::NoCurrentThread;
  {
    Guard guard;
    if (queue_for_current_locked()) return {};
  }
  if (!self->owner_process) return KernelError::NoCurrentProcess;
  void *p = KE_Malloc(sizeof(MessageQueue));
  if (!p) return KernelError::OutOfMemory;
  auto *q = new (p) MessageQueue{};
  q->thread = self.get();
  q->process = self->owner_process;
  q->header.manualReset = true;
  {
    Guard guard;
    if (!queue_for_current_locked()) {
      q->next = g_queues;
      g_queues = q;
      return {};
    }
  }
  q->~MessageQueue();
  KE_Free(q);
  return {};
}

void wake_locked(MessageQueue &queue) noexcept {
  KE_EVENT_Signal(queue.header, true);
}

kstd::error_code post_locked(MessageQueue &queue, uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam,
                             uint32_t keyState) noexcept {
  const QueuedMessage m{.hwnd = hwnd, .message = message, .wParam = wParam, .lParam = lParam,
                        .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000),
                        .keyState = keyState};
  if (const kstd::error_code pushed = queue.ring.Push(m); pushed) return pushed;
  wake_locked(queue);
  return {};
}

kstd::error_code post_message(uint32_t hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) noexcept {
  if (!hwnd) {
    if (const auto error = ensure_current_queue(); error) return error;
  }
  Guard guard;
  if (hwnd == 0) {
    const auto queue = queue_for_current_locked();
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

bool font_message_locked(const MessageQueue &queue, QueuedMessage &out, bool remove) noexcept {
  for (auto *w = topmost_locked(); w; w = w->below) {
    if (w->queue != &queue || !w->pendingFontGeneration) continue;
    out = QueuedMessage{.hwnd = w->hwnd, .message = kMsgFontChanged,
                        .wParam = w->pendingFontGeneration, .lParam = 0,
                        .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
    if (remove) w->pendingFontGeneration = 0;
    return true;
  }
  return false;
}

kstd::expected<QueuedMessage, kstd::error_code> get_message(bool block, bool remove) noexcept {
  if (const auto error = ensure_current_queue(); error) return kstd::unexpected(error);
  for (;;) {
    lock();
    const auto queue = queue_for_current_locked();
    if (!queue) {
      unlock();
      return kstd::unexpected(queue.error());
    }
    MessageQueue *q = *queue;
    QueuedMessage out{};
    if (font_message_locked(*q, out, remove)) {
      unlock();
      return out;
    }
    if (should_serve_paint_before_input_locked(*q)) {
      const auto *w = paint_candidate_locked(*q);
      out = QueuedMessage{.hwnd = w->hwnd, .message = kMsgPaint, .wParam = 0,
                          .lParam = 0,
                          .time = static_cast<uint32_t>(KE_TIME_GetSystemTime().count() / 1000000)};
      unlock();
      return out;
    }
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
    if (const kstd::error_code waited = alertable_wait(q->header, kstd::chrono::milliseconds(-1));
        waited) return kstd::unexpected(waited);
  }
}

kstd::error_code wait_message(uint32_t timeoutMs) noexcept {
  const auto pending = get_message(false, false);
  if (pending) return {};
  if (pending.error() != KernelError::WouldBlock) return pending.error();
  // get_message reset the header under the lock, so a post after that point
  // sets it again and the wait below returns at once.
  lock();
  const auto queue = queue_for_current_locked();
  if (!queue) {
    unlock();
    return queue.error();
  }
  MessageQueue *q = *queue;
  unlock();
  const int64_t ms = timeoutMs == UINT32_MAX ? -1 : static_cast<int64_t>(timeoutMs);
  return alertable_wait(q->header, kstd::chrono::milliseconds(ms));
}

MessageQueue *free_queues_locked(KProcessObject &proc) noexcept {
  MessageQueue *detached = nullptr;
  for (MessageQueue **it = &g_queues; *it;) {
    MessageQueue *q = *it;
    if (q->process == &proc) {
      *it = q->next;
      q->next = detached;
      detached = q;
    } else {
      it = &q->next;
    }
  }
  return detached;
}

void release_queues(MessageQueue *queue) noexcept {
  while (queue) {
    auto *next = queue->next;
    queue->~MessageQueue();
    KE_Free(queue);
    queue = next;
  }
}

}// namespace kernel::user
