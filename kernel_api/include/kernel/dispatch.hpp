#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KInterruptServiceRoutineObject.hpp>

namespace kernel {

struct HalInterruptRecord {
  uint32_t vector{0};
  uint32_t irq{0};
  uint64_t timestamp{0};
  uint32_t flags{0};
};

class InterruptRecordRing {
 public:
  static constexpr size_t kCapacity = 256;

  // Single-producer (ISR) safe: only touches m_head, reads m_tail, never blocks or allocates
  KernelError Push(const HalInterruptRecord &record) noexcept {
    const size_t head = m_head;
    const size_t nextHead = (head + 1) % kCapacity;
    if (nextHead == m_tail) {
      return KernelError::QueueFull;
    }
    m_records[head] = record;
    m_head = nextHead;
    return KernelError::Success;
  }

  // Single-consumer (thread) safe: touches m_tail, reads m_head
  bool Pop(HalInterruptRecord &out) noexcept {
    const size_t tail = m_tail;
    if (tail == m_head) {
      return false;
    }
    out = m_records[tail];
    m_tail = (tail + 1) % kCapacity;
    return true;
  }

  [[nodiscard]] bool Empty() const noexcept { return m_head == m_tail; }

  [[nodiscard]] size_t Size() const noexcept {
    const size_t head = m_head;
    const size_t tail = m_tail;
    if (head >= tail) {
      return head - tail;
    }
    return kCapacity - (tail - head);
  }

  void Reset() noexcept {
    m_head = 0;
    m_tail = 0;
  }

 private:
  volatile size_t m_head{0};
  volatile size_t m_tail{0};
  HalInterruptRecord m_records[kCapacity]{};
};

} // namespace kernel

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_EnqueueInterrupt
 *
 * Called from HAL interrupt gates (DIRQL) to push an interrupt occurrence into
 * the kernel's lock-free ring buffer and awaken the dispatch thread.
 * ISR-safe: non-blocking, no allocation, no CPU interrupt manipulation.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_DISPATCH_EnqueueInterrupt,
    const kernel::HalInterruptRecord &record);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_RegisterHandler
 *
 * Registers an ISR object with the dispatch subsystem for the given IRQ line.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_DISPATCH_RegisterHandler,
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_UnregisterHandler
 *
 * Unregisters any ISR object connected to the given IRQ line.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KernelError,
    KE_DISPATCH_UnregisterHandler,
    uint32_t irq);
