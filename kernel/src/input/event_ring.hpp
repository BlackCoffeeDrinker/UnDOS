#pragma once

#include <kernel/input.hpp>

// Pure, header-only bounded AnyInputEvent queue with O(1) push and pop. No
// kernel calls and no locking of its own: the input manager
// (kernel/src/input/input_manager.cpp) wraps every access in an irqsave
// spinlock, since several producer *threads* now exist (the i8042 pump, the
// gameport poll thread, a future USB HID worker). Freestanding, so it
// compiles unchanged into KernelTests (see
// hosted_test/tests/input_queue_test.cpp).
namespace kernel::input {

// Returns true for a MouseInputEvent that carries pure motion -- no button
// transition and no wheel delta -- the only kind of event this ring is
// allowed to coalesce when full.
[[nodiscard]] inline bool IsPureMotionMouseEvent(const AnyInputEvent &event) noexcept {
  const MouseInputEvent *mouse = event.base.as<MouseInputEvent>();
  return mouse != nullptr && mouse->buttonFlags == 0;
}

class EventRing {
  public:
  static constexpr size_t kCapacity = 128;

  // Always succeeds and never blocks. On a full ring:
  //  - a pure-motion pointer event accumulates its deltas into the newest
  //    unread motion entry, so continuous movement stays smooth under flood
  //    without consuming a slot;
  //  - anything else (a button transition, a wheel delta, any keyboard
  //    event) always appends, displacing the oldest entry. Clicks and
  //    keystrokes are therefore never lost in favour of motion.
  void Push(const AnyInputEvent &event) noexcept {
    if (m_count < kCapacity) {
      m_items[m_head] = event;
      m_head = Next(m_head);
      ++m_count;
      return;
    }

    if (IsPureMotionMouseEvent(event)) {
      const size_t newest = Prev(m_head);
      if (IsPureMotionMouseEvent(m_items[newest])) {
        MouseInputEvent &target = m_items[newest].mouse;
        const MouseInputEvent &source = event.mouse;
        // Absolute reports are latest-value-wins; relative deltas add up,
        // so the total displacement survives the coalesce.
        if (source.isMovementAbsolute) {
          target.isMovementAbsolute = true;
          target.lastX = source.lastX;
          target.lastY = source.lastY;
        } else if (!target.isMovementAbsolute) {
          target.lastX += source.lastX;
          target.lastY += source.lastY;
        } else {
          target = source;
        }
        target.timestamp = source.timestamp;
        return;
      }
    }

    // No coalescable motion entry: drop the oldest event to make room.
    m_tail = Next(m_tail);
    --m_count;
    m_items[m_head] = event;
    m_head = Next(m_head);
    ++m_count;
  }

  // Pops the oldest event (FIFO). Returns false, leaving `out` untouched,
  // when the ring is empty.
  bool Pop(AnyInputEvent &out) noexcept {
    if (m_count == 0) {
      return false;
    }
    out = m_items[m_tail];
    m_tail = Next(m_tail);
    --m_count;
    return true;
  }

  [[nodiscard]] size_t Count() const noexcept { return m_count; }
  [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }
  [[nodiscard]] bool Full() const noexcept { return m_count == kCapacity; }

  private:
  static constexpr size_t Next(size_t index) noexcept { return (index + 1) % kCapacity; }
  static constexpr size_t Prev(size_t index) noexcept { return (index + kCapacity - 1) % kCapacity; }

  AnyInputEvent m_items[kCapacity];
  size_t m_head = 0;// next write
  size_t m_tail = 0;// next read
  size_t m_count = 0;
};

}// namespace kernel::input
