#pragma once

#include <stdint.h>

namespace kernel::user {

struct QueuedMessage {
  uint32_t hwnd{0};
  uint32_t message{0};
  uint32_t wParam{0};
  uint32_t lParam{0};
  uint32_t time{0};
};

// Fixed-size FIFO of messages for one thread. Not synchronised: the caller
// holds the window manager lock. Consecutive mouse moves to the same window
// are merged into the newest one.
template<uint32_t N, uint32_t MouseMoveMessage>
struct MessageRing {
  QueuedMessage slots[N]{};
  uint32_t head{0};
  uint32_t count{0};

  [[nodiscard]] bool Empty() const noexcept { return count == 0; }
  [[nodiscard]] bool Full() const noexcept { return count == N; }

  // Returns false when the ring is full (the message is dropped).
  bool Push(const QueuedMessage &m) noexcept {
    if (count > 0 && m.message == MouseMoveMessage) {
      QueuedMessage &last = slots[(head + count - 1) % N];
      if (last.message == MouseMoveMessage && last.hwnd == m.hwnd) {
        last = m;
        return true;
      }
    }
    if (Full()) return false;
    slots[(head + count) % N] = m;
    ++count;
    return true;
  }

  bool Peek(QueuedMessage &out) const noexcept {
    if (Empty()) return false;
    out = slots[head];
    return true;
  }

  bool Pop(QueuedMessage &out) noexcept {
    if (!Peek(out)) return false;
    head = (head + 1) % N;
    --count;
    return true;
  }

  // Drops every message addressed to hwnd (used when a window is destroyed).
  void RemoveFor(uint32_t hwnd) noexcept {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const QueuedMessage m = slots[(head + i) % N];
      if (m.hwnd != hwnd) slots[(head + kept++) % N] = m;
    }
    count = kept;
  }
};

} // namespace kernel::user
