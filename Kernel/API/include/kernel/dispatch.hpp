#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/kerror.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KInterruptServiceRoutineObject.hpp>

namespace kernel {

// Record flag bits (HalInterruptRecord::flags). The HAL sets the level bit
// when the source is level-triggered: the gate masked the line before EOI and
// the dispatcher must call HAL_INTR_CompleteInterrupt(irq) after the dequeue
// callback snapshot finishes so the HAL can unmask it. Edge sources leave the
// bit clear and HAL_INTR_CompleteInterrupt is a no-op for them.
inline constexpr uint32_t kHalInterruptRecordLevelTriggered = 1u << 0;

struct HalInterruptRecord {
  uint32_t vector{0};
  // Opaque HAL interrupt identifier used to match this event to registered handlers.
  uint32_t irq{0};
  uint64_t timestamp{0};
  uint32_t flags{0};
};

struct KDpc;

// Deferred callback contract: runs on the dispatch worker's ordinary thread
// context (no DISPATCH_LEVEL), with a worker-local copy of the first-wins
// argument snapshot. No locks are held across the invocation.
using KDpcRoutine = cfunc<void(KDpc &dpc, void *context, const HalInterruptRecord &args)>;

// Generic deferred procedure call object.
//
// `state` is a 32-bit atomic word manipulated only through HAL_ATOMIC_* and
// implements the first-wins/logical-cancel contract:
//  - A producer atomically sets kStatePending only when no invocation is
//    pending; only that winning producer writes `args`.
//  - KE_DPC_Cancel sets kStateCancelled only while kStatePending is set; the
//    worker consumes the cancelled entry, skips the callback and clears both
//    bits.
//  - The worker copies `args` while kStatePending is still set, then claims
//    the invocation (kStatePending -> kStateRunning, release ordering)
//    immediately before invoking the callback. On completion kStateRunning is
//    cleared while preserving any later kStatePending.
//
// Storage lifetime: the KDpc must stay registered and its storage stable
// until it is unregistered with no queued or in-flight invocation. A queue
// slot stores only this pointer; registration owns the lifetime guarantee.
struct KDpc {
  static constexpr uint32_t kStatePending = 1u << 0;
  static constexpr uint32_t kStateCancelled = 1u << 1;
  static constexpr uint32_t kStateRunning = 1u << 2;

  uint32_t state{0};
  // First-wins argument snapshot. Owned by the producer that won the
  // kStatePending transition until the worker copies it; ordinary data, no
  // 64-bit atomics on the hot path.
  HalInterruptRecord args{};
  KDpcRoutine routine{};
  void *context{nullptr};
  // Interrupt-source bookkeeping (system-owned source DPCs only).
  uint32_t sourceIrq{0};
  bool isInterruptSource{false};
  bool registered{false};
};

// Bounded multi-producer/single-consumer DPC pointer ring using per-slot
// 32-bit sequence numbers (Vyukov-style). Producers reserve a position with
// 32-bit compare-exchange, write the pointer, then publish the slot sequence
// with release ordering; the single consumer observes the sequence with
// acquire ordering and releases the slot for the next ring generation.
// Full/empty is detected by comparing the slot sequence with the expected
// position, so all 256 slots are usable (no head+1==tail sentinel). Sequence
// arithmetic is wrap-safe: the capacity is far below the signed 32-bit
// half-range. Push never waits for the consumer; compare-exchange can retry
// under producer contention, so this is bounded storage, not wait-free.
class DpcQueue {
 public:
  static constexpr uint32_t kCapacity = 256;// power of two, all slots usable
  static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");

  // Constant-initialisable: a global ring is ready without a constructor walk.
  constexpr DpcQueue() noexcept {
    for (uint32_t i = 0; i < kCapacity; ++i) m_slots[i].sequence = i;
  }

  // Multi-producer safe, IRQ safe: no allocation, no callbacks, no waiting.
  bool Push(KDpc *dpc) noexcept {
    uint32_t pos = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&m_enqueuePos), PlatformMemoryOrder::Relaxed);
    for (;;) {
      Slot &slot = m_slots[pos & (kCapacity - 1)];
      const uint32_t seq = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&slot.sequence), PlatformMemoryOrder::Acquire);
      const auto dif = static_cast<int32_t>(seq - pos);
      if (dif == 0) {
        uint32_t expected = pos;
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&m_enqueuePos), expected, pos + 1, PlatformMemoryOrder::Relaxed)) {
          slot.item = dpc;
          HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&slot.sequence), pos + 1, PlatformMemoryOrder::Release);
          return true;
        }
        pos = expected;// lost the race, retry at the refreshed position
      } else if (dif < 0) {
        return false;// full: slot still holds the previous generation
      } else {
        pos = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&m_enqueuePos), PlatformMemoryOrder::Relaxed);
      }
    }
  }

  // Single-consumer only (the dispatch worker).
  bool Pop(KDpc *&out) noexcept {
    const uint32_t pos = m_dequeuePos;
    Slot &slot = m_slots[pos & (kCapacity - 1)];
    const uint32_t seq = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&slot.sequence), PlatformMemoryOrder::Acquire);
    const auto dif = static_cast<int32_t>(seq - (pos + 1));
    if (dif < 0) return false;// empty: producer has not published this slot
    out = slot.item;
    slot.item = nullptr;
    // Release the slot for the next ring generation.
    HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&slot.sequence), pos + kCapacity, PlatformMemoryOrder::Release);
    m_dequeuePos = pos + 1;
    return true;
  }

  [[nodiscard]] bool Empty() const noexcept {
    const uint32_t pos = m_dequeuePos;
    const Slot &slot = m_slots[pos & (kCapacity - 1)];
    const uint32_t seq = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&slot.sequence), PlatformMemoryOrder::Acquire);
    return static_cast<int32_t>(seq - (pos + 1)) < 0;
  }

  void Reset() noexcept {
    for (uint32_t i = 0; i < kCapacity; ++i) {
      m_slots[i].sequence = i;
      m_slots[i].item = nullptr;
    }
    m_enqueuePos = 0;
    m_dequeuePos = 0;
  }

 private:
  struct Slot {
    uint32_t sequence{0};
    KDpc *item{nullptr};
  };

  Slot m_slots[kCapacity]{};
  uint32_t m_enqueuePos{0};
  uint32_t m_dequeuePos{0};// consumer-owned, no atomics needed
};

} // namespace kernel

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_EnqueueInterrupt
 *
 * Called from HAL interrupt gates (DIRQL) to record an interrupt occurrence.
 * The record is the first-wins argument snapshot of the line's system-owned
 * source DPC: the first occurrence while the DPC is not pending captures the
 * record and publishes one pointer in the common MPSC DPC ring; later
 * occurrences coalesce until the worker consumes it. ISR-safe: non-blocking,
 * no allocation, no CPU interrupt manipulation.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_DISPATCH_EnqueueInterrupt,
    const kernel::HalInterruptRecord &record);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_RegisterHandler
 *
 * Registers an ISR object with the dispatch subsystem for the given
 * platform-specific interrupt identifier. The kernel does not impose a
 * controller-specific range on this identifier. The first registration for a
 * line reserves one source-DPC queue slot (failing with
 * ResourceLimitExceeded when the reservation budget is exhausted) and
 * connects the HAL interrupt source, atomically with the first-registration
 * decision (a failed HAL connection returns ResourceConflict with no side
 * effects); later compatible registrations share that line and reservation.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_DISPATCH_RegisterHandler,
    uint32_t interruptId,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_UnregisterHandler
 *
 * Unregisters one specific (interruptId, isr) registration. Other handlers
 * sharing the line are unaffected. Before returning, waits for any in-flight
 * dequeue-snapshot callbacks of the line to finish (rundown), so the caller
 * may free the handler's borrowed service_context afterwards; calling this
 * from the handler's own dispatch callback is rejected with ResourceConflict
 * (it would self-deadlock on that wait). Removing the final handler
 * disconnects the HAL source atomically with the last-handler decision; the
 * line's source reservation is released only once any still-queued source-DPC
 * pointer has been consumed by the worker.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_DISPATCH_UnregisterHandler,
    uint32_t interruptId,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DISPATCH_HandlerCount
 *
 * Returns the number of handlers currently registered for the given
 * platform-specific interrupt identifier.
 */
UNDOS_KERNEL_PUBLIC_V1API(size_t, KE_DISPATCH_HandlerCount, uint32_t interruptId);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DPC_Register
 *
 * Registers a generic DPC with the dispatch subsystem, reserving one queue
 * slot so normal operation can never exceed the common ring's capacity.
 * Fails with ResourceLimitExceeded when the reservation budget is exhausted.
 * The caller owns the KDpc storage, which must remain valid until
 * KE_DPC_Unregister succeeds.
 */
UNDOS_KERNEL_PUBLIC_V1API(kstd::error_code, KE_DPC_Register, kernel::KDpc &dpc);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DPC_Unregister
 *
 * Releases a generic DPC's queue reservation. Fails with ResourceConflict
 * while an invocation is still queued or in flight; the caller must retry
 * after the worker has consumed it (rundown).
 */
UNDOS_KERNEL_PUBLIC_V1API(kstd::error_code, KE_DPC_Unregister, kernel::KDpc &dpc);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DPC_Queue
 *
 * Queues one invocation of a registered DPC with first-wins argument
 * capture: if no invocation is pending the caller wins, captures `args`, and
 * publishes one pointer in the common ring; otherwise the occurrence
 * coalesces into the already-pending invocation and `args` is discarded.
 * Note that an occurrence that coalesces into a pending invocation that was
 * already cancelled (KE_DPC_Cancel won, worker has not consumed it yet) is
 * skipped along with it: success does not guarantee an eventual callback.
 * Producers needing exactness must re-queue after the cancelled entry has
 * been consumed or drain authoritative device state. Producer-safe from
 * interrupt or thread context: no allocation, no callbacks, no waiting for
 * the worker.
 */
UNDOS_KERNEL_PUBLIC_V1API(
    kstd::error_code,
    KE_DPC_Queue,
    kernel::KDpc &dpc,
    const kernel::HalInterruptRecord &args);

/**
 * @ingroup DISPATCH
 * @brief Method KE_DPC_Cancel
 *
 * Logical cancellation: atomically sets the cancelled bit only while an
 * invocation is pending and not already cancelled. Returns true only when
 * this transition wins the race against the worker's start claim; that
 * callback is then skipped. Returns false when nothing is pending, the
 * pending invocation was already cancelled, or the worker's claim won. The
 * cancelled pointer stays in the ring until the worker consumes it; requeue
 * is possible only after that consumption. DPC storage reclamation still
 * requires unregistration/rundown.
 */
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_DPC_Cancel, kernel::KDpc &dpc);
