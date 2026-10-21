#pragma once

#include <stddef.h>
#include <stdint.h>

// Single-producer/single-consumer byte ring, lifted out of the kernel's
// input manager when decoding moved into this driver. The producer (the
// i8042 ISR) only ever writes `head`; the consumer (the pump thread) only
// ever writes `tail`. No locks: kernel::SpinLock is documented as not
// ISR-safe (kernel/spinlock.hpp), and this ring must be safe to push from
// interrupt context.
//
// Each ring belongs to exactly one device and one interrupt source, so the
// single-producer invariant is stronger here than it was in the kernel.
// Freestanding, so it compiles into KernelTests (see
// hosted_test/tests/ps2_byte_ring_test.cpp).
namespace ps2 {

class ByteRing {
  public:
  static constexpr size_t kCapacity = 256;

  // ISR-safe: only touches `head` (and reads `tail`), never blocks.
  bool Push(uint8_t byte) noexcept {
    const size_t head = m_head;
    const size_t nextHead = (head + 1) % kCapacity;
    if (nextHead == m_tail) {
      // Ring full: drop the byte rather than overwrite unread data or spin.
      return false;
    }
    m_buffer[head] = byte;
    m_head = nextHead;
    return true;
  }

  // Consumer-side only: touches `tail` (and reads `head`).
  bool Pop(uint8_t &out) noexcept {
    const size_t tail = m_tail;
    if (tail == m_head) {
      return false;
    }
    out = m_buffer[tail];
    m_tail = (tail + 1) % kCapacity;
    return true;
  }

  [[nodiscard]] bool Empty() const noexcept { return m_head == m_tail; }

  private:
  volatile size_t m_head = 0;
  volatile size_t m_tail = 0;
  uint8_t m_buffer[kCapacity]{};
};

}// namespace ps2
