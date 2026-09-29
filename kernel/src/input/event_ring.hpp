#pragma once

#include <kernel/input.hpp>

// Pure, header-only bounded AnyInputEvent queue. No kernel calls: the input
// manager's pump thread (kernel/src/input/input_manager.cpp) is the only
// producer, guarding pushes/pops with its own kernel::Mutex since, unlike
// the byte rings, this queue is drained from an ordinary thread context
// (KE_INPUT_DequeueEvent), never from an ISR. Freestanding, so it compiles
// unchanged into KernelTests (see hosted_test/tests/input_queue_test.cpp),
// exactly like mouse_packet.hpp / key_translate.hpp.
namespace kernel::input {

// Returns true for a MouseInputEvent that carries pure motion -- no button
// transition and no wheel delta -- the only kind of event this ring is
// allowed to coalesce/drop when full (Task 16's overflow policy).
[[nodiscard]] inline bool IsPureMotionMouseEvent(const AnyInputEvent &event) noexcept {
  const MouseInputEvent *mouse = event.base.as<MouseInputEvent>();
  return mouse != nullptr && mouse->buttonFlags == 0;
}

class EventRing {
  public:
  static constexpr size_t kCapacity = 128;

  // Always succeeds: on a full ring, the oldest pure-motion mouse event is
  // coalesced away first (Task 16); if none exists, the oldest event of any
  // kind is dropped instead -- the queue itself never blocks the caller and
  // never grows past kCapacity.
  void Push(const AnyInputEvent &event) noexcept {
    if (m_count < kCapacity) {
      m_items[m_count++] = event;
      return;
    }

    size_t dropIndex = 0;
    bool foundMotion = false;
    for (size_t i = 0; i < m_count; ++i) {
      if (IsPureMotionMouseEvent(m_items[i])) {
        dropIndex = i;
        foundMotion = true;
        break;
      }
    }
    // No coalescable motion event survived the scan above: fall back to
    // dropping the oldest event overall (index 0), per the documented
    // "never block" overflow policy.
    if (!foundMotion) {
      dropIndex = 0;
    }

    for (size_t i = dropIndex; i + 1 < m_count; ++i) {
      m_items[i] = m_items[i + 1];
    }
    m_items[m_count - 1] = event;
  }

  // Pops the oldest event (FIFO). Returns false, leaving `out` untouched,
  // when the ring is empty.
  bool Pop(AnyInputEvent &out) noexcept {
    if (m_count == 0) {
      return false;
    }
    out = m_items[0];
    for (size_t i = 0; i + 1 < m_count; ++i) {
      m_items[i] = m_items[i + 1];
    }
    --m_count;
    return true;
  }

  [[nodiscard]] size_t Count() const noexcept { return m_count; }
  [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }
  [[nodiscard]] bool Full() const noexcept { return m_count == kCapacity; }

  private:
  AnyInputEvent m_items[kCapacity];
  size_t m_count = 0;
};

}// namespace kernel::input
