#pragma once

#include <kernel/dispatch.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <mutex.hpp>

namespace kernel::dispatch {

void init() noexcept;

// Common deferred-service core shared by the kernel dispatch worker and the
// hosted test harness. One bounded MPSC DPC ring serves both generic DPCs and
// system-owned interrupt-source DPCs; registration reserves queue capacity so
// normal operation can never exceed the ring (each registered DPC holds at
// most one queue entry).
//
// Producer paths (QueueDpc/EnqueueInterrupt) are IRQ-safe: no allocation, no
// callbacks, no registry lock, no waiting for the worker. The registry lock
// only guards handler-list mutation and the worker's dequeue snapshot, and is
// never held across a callback.
class DpcCore {
 public:
  static constexpr uint32_t kQueueCapacity = DpcQueue::kCapacity;
  static constexpr size_t kMaxSources = 64;
  static constexpr size_t kMaxHandlersPerInterrupt = 4;

 private:
  // pinState bit 31 is the published active flag; bits 0-30 count producer
  // pins currently inside EnqueueInterrupt for this slot.
  static constexpr uint32_t kSourceActiveBit = 1u << 31;

  struct Source {
    uint32_t pinState{0};// 32-bit active bit + producer pin refcount
    uint32_t irq{0};
    KDpc dpc{};
    KObjectPtr<KInterruptServiceRoutineObject> handlers[kMaxHandlersPerInterrupt]{};
    size_t count{0};
    uint32_t inFlight{0};     // dequeue snapshots currently running callbacks
    uint32_t pendingRelease{0};// reservation release deferred past rundown
  };

 public:
  // `reservationLimit` exists only so hosted tests can force the
  // "unexpected queue full" diagnostic path, which capacity reservation
  // makes unreachable through the public contract. The kernel uses the
  // default, which equals the ring capacity.
  // constexpr so the kernel's global core is constant-initialised (constinit)
  // and never depends on the image's constructor walk. The member
  // initialisers equal the Reset() state.
  constexpr explicit DpcCore(uint32_t reservationLimit = kQueueCapacity) noexcept
      : m_reservationLimit(reservationLimit) {}

  void Reset() noexcept {
    m_queue.Reset();
    for (auto &source: m_sources) {
      source.pinState = 0;
      source.irq = 0;
      source.count = 0;
      for (auto &handler: source.handlers) handler = nullptr;
      source.dpc = KDpc{};
      source.inFlight = 0;
      source.pendingRelease = 0;
    }
    m_reserved = 0;
    m_queueFullDiagnostics = 0;
    m_workerThreadId = nullptr;
  }

  // Identity source for the thread currently executing; used only to detect a
  // disconnect issued from the dispatch worker's own callback (which would
  // self-deadlock on the rundown wait). The kernel wires this to the
  // scheduler's current thread; hosted tests install their own source.
  void SetThreadIdSource(void *(*fn)() noexcept) noexcept { m_threadIdFn = fn; }


  // --- Generic DPC contract -------------------------------------------------

  kstd::error_code RegisterDpc(KDpc &dpc) noexcept {
    kstd::unique_lock guard(m_registryLock);
    if (dpc.registered) return {};
    if (m_reserved >= m_reservationLimit) return KernelError::ResourceLimitExceeded;
    ++m_reserved;
    dpc.state = 0;
    dpc.isInterruptSource = false;
    dpc.registered = true;
    return {};
  }

  kstd::error_code UnregisterDpc(KDpc &dpc) noexcept {
    kstd::unique_lock guard(m_registryLock);
    if (!dpc.registered) return {};
    // Rundown: a queued pointer or in-flight callback still references this
    // storage; the caller must retry after the worker has consumed it.
    if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Acquire) != 0) {
      return KernelError::ResourceConflict;
    }
    dpc.registered = false;
    --m_reserved;
    return {};
  }

  // First-wins queueing: only the producer that wins the pending transition
  // writes the argument snapshot and publishes exactly one ring pointer.
  kstd::error_code QueueDpc(KDpc &dpc, const HalInterruptRecord &args) noexcept {
    if (!dpc.registered) return KernelError::InvalidArgument;
    for (;;) {
      uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Relaxed);
      if (state & KDpc::kStatePending) return {};// coalesced; args stay first-wins
      if (HAL_ATOMIC_CompareExchange32(
              VirtualAddress::from_ptr(&dpc.state), state,
              state | KDpc::kStatePending, PlatformMemoryOrder::Acquire)) {
        break;// this producer won and owns the argument snapshot
      }
    }
    dpc.args = args;
    if (!m_queue.Push(&dpc)) {
      // Unexpected: capacity reservation should make this unreachable. Record
      // a bounded diagnostic and clear the queued state so the DPC cannot
      // stay permanently stuck; never invoke the callback inline. For a
      // level source the line stays masked (fail-safe outage); an edge
      // occurrence may be lost and the driver must drain device state.
      HAL_ATOMIC_FetchAdd32(
          VirtualAddress::from_ptr(&m_queueFullDiagnostics), 1, PlatformMemoryOrder::Relaxed);
      for (;;) {
        uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Relaxed);
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&dpc.state), state,
                state & ~(KDpc::kStatePending | KDpc::kStateCancelled),
                PlatformMemoryOrder::Release)) {
          break;
        }
      }
      return KernelError::QueueFull;
    }
    return {};
  }

  // Logical cancellation: wins only against a pending, not-yet-claimed,
  // not-yet-cancelled invocation.
  bool CancelDpc(KDpc &dpc) noexcept {
    for (;;) {
      uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Relaxed);
      if (!(state & KDpc::kStatePending) || (state & KDpc::kStateCancelled)) return false;
      if (HAL_ATOMIC_CompareExchange32(
              VirtualAddress::from_ptr(&dpc.state), state,
              state | KDpc::kStateCancelled, PlatformMemoryOrder::AcquireRelease)) {
        return true;
      }
    }
  }

  // --- Interrupt-source registry ---------------------------------------------

  // Registers `isr` for `interruptId`. The first registration for a line
  // connects the HAL source and activates one system-owned source DPC
  // consuming one queue reservation; later registrations share the line.
  // Duplicate (irq, isr) pairs are idempotent. The first/last determination
  // and the HAL connect are made atomically under the registry lock (the HAL
  // source operations take no kernel locks; documented lock ordering:
  // registry lock may be held across HAL_INTR_Connect/DisconnectInterrupt).
  kstd::error_code RegisterHandler(
      uint32_t interruptId,
      const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept {
    if (!isr) return KernelError::InvalidArgument;
    kstd::unique_lock guard(m_registryLock);

    Source *freeSlot = nullptr;
    bool sawRundownSlot = false;
    for (auto &source: m_sources) {
      const uint32_t pinState = HAL_ATOMIC_Load32(
          VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire);
      if ((pinState & kSourceActiveBit) && source.irq == interruptId) {
        for (size_t i = 0; i < source.count; ++i) {
          if (source.handlers[i] == isr) return {};
        }
        if (source.count >= kMaxHandlersPerInterrupt) return KernelError::ResourceLimitExceeded;
        source.handlers[source.count++] = isr;
        return {};
      }
      if (pinState & kSourceActiveBit) continue;
      // The source slot may only be recycled once its previous queued pointer
      // has been consumed, all producer pins have drained, and its deferred
      // reservation release has happened; otherwise it is still in rundown.
      if (pinState != 0 || source.pendingRelease != 0 ||
          HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.dpc.state), PlatformMemoryOrder::Acquire) != 0) {
        sawRundownSlot = true;
        continue;
      }
      if (!freeSlot) freeSlot = &source;
    }

    if (!freeSlot) return sawRundownSlot ? KernelError::ResourceConflict : KernelError::ResourceLimitExceeded;
    if (m_reserved >= m_reservationLimit) return KernelError::ResourceLimitExceeded;

    // First registration for the line: connect the HAL source while still
    // holding the registry lock so a concurrent register/unregister cannot
    // race the first/last decision (TOCTOU). Nothing has been mutated yet, so
    // a failed connection needs no rollback.
    if (!HAL_INTR_ConnectInterrupt(interruptId, isr)) {
      return KernelError::ResourceConflict;
    }

    ++m_reserved;
    freeSlot->irq = interruptId;
    freeSlot->handlers[0] = isr;
    freeSlot->count = 1;
    freeSlot->dpc = KDpc{};
    freeSlot->dpc.sourceIrq = interruptId;
    freeSlot->dpc.isInterruptSource = true;
    freeSlot->dpc.registered = true;
    // Publish the slot last so a concurrent producer scanning the table never
    // observes a half-initialized source.
    HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&freeSlot->pinState), kSourceActiveBit, PlatformMemoryOrder::Release);
    return {};
  }

  // Removes exactly one (interruptId, isr) registration; other handlers on
  // the shared line are unaffected. Waits for any in-flight dequeue snapshot
  // of the line to finish before returning, so the caller may free the
  // handler's borrowed service_context afterwards (rundown). The final
  // removal disconnects the HAL source (atomically with the last-handler
  // decision, under the registry lock), prevents new producer pins, drains
  // old pins, and defers the reservation release until any still-queued ring
  // pointer has been consumed. A disconnect issued from the dispatch worker's
  // own callback is rejected with ResourceConflict (it would self-deadlock on
  // the rundown wait).
  kstd::error_code UnregisterHandler(
      uint32_t interruptId,
      const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept {
    Source *rundown = nullptr;
    bool teardown = false;
    {
      kstd::unique_lock guard(m_registryLock);
      for (auto &source: m_sources) {
        if (!(HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire) & kSourceActiveBit) ||
            source.irq != interruptId)
          continue;
        for (size_t i = 0; i < source.count; ++i) {
          if (source.handlers[i] != isr) continue;
          // Snapshot increments happen under this lock, so this check is
          // atomic with respect to the worker starting a new snapshot.
          if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.inFlight), PlatformMemoryOrder::Acquire) != 0 &&
              m_threadIdFn && m_workerThreadId && m_threadIdFn() == m_workerThreadId) {
            return KernelError::ResourceConflict;// self-disconnect from own callback
          }
          for (size_t j = i; j + 1 < source.count; ++j) {
            source.handlers[j] = source.handlers[j + 1];
          }
          source.handlers[--source.count] = nullptr;
          if (source.count == 0) {
            // Last handler: mask/disconnect the HAL source first (atomic
            // with the last-removal decision), then prevent new producer
            // pins by clearing the active bit while preserving the pin
            // count of any producer already inside the enqueue path.
            HAL_INTR_DisconnectInterrupt(interruptId);
            for (;;) {
              uint32_t pinState = HAL_ATOMIC_Load32(
                  VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Relaxed);
              if (HAL_ATOMIC_CompareExchange32(
                      VirtualAddress::from_ptr(&source.pinState), pinState,
                      pinState & ~kSourceActiveBit, PlatformMemoryOrder::Release)) {
                break;
              }
            }
            source.dpc.registered = false;
            // The reservation is released only after the source is disabled,
            // producer pins are drained, and queued work has been consumed;
            // otherwise a still-queued ring pointer could transiently push
            // occupancy above the reserved count.
            source.pendingRelease = 1;
            teardown = true;
          }
          rundown = &source;
          break;
        }
        if (!rundown) return KernelError::InvalidArgument;
        break;
      }
      if (!rundown) return KernelError::InvalidArgument;
    }

    // Rundown: wait outside the lock until no dequeue snapshot that may
    // contain this handler is still running its callbacks.
    while (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&rundown->inFlight), PlatformMemoryOrder::Acquire) != 0) {
      KE_SCHED_Yield();
    }
    if (teardown) {
      // Drain producer pins (bounded: a pin lives only inside the IRQ
      // producer's enqueue path and the active bit is already clear).
      while (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&rundown->pinState), PlatformMemoryOrder::Acquire) != 0) {
        KE_SCHED_Yield();
      }
      kstd::unique_lock guard(m_registryLock);
      TryReleaseSourceReservationLocked(*rundown);
    }
    return {};
  }

  size_t HandlerCount(uint32_t interruptId) noexcept {
    kstd::unique_lock guard(m_registryLock);
    for (auto &source: m_sources) {
      if ((HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire) & kSourceActiveBit) &&
          source.irq == interruptId)
        return source.count;
    }
    return 0;
  }

  // IRQ gate producer: coalesces the occurrence into the line's source DPC
  // (first-wins record capture) and publishes at most one ring pointer.
  // Lock-free with respect to the registry lock. The producer pins the source
  // descriptor (active bit + pin refcount in one 32-bit word), revalidates the
  // identifier after pinning, and unpins after the enqueue/coalesce, so
  // teardown on another CPU can wait for pins before recycling the slot.
  kstd::error_code EnqueueInterrupt(const HalInterruptRecord &record) noexcept {
    for (auto &source: m_sources) {
      uint32_t pinState = HAL_ATOMIC_Load32(
          VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire);
      if (!(pinState & kSourceActiveBit) || source.irq != record.irq) continue;
      // Pin: increment the refcount only while the active bit is still set.
      bool pinned = false;
      while (pinState & kSourceActiveBit) {
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&source.pinState), pinState,
                pinState + 1, PlatformMemoryOrder::Acquire)) {
          pinned = true;
          break;
        }
      }
      if (!pinned) continue;// torn down concurrently; keep scanning
      // Post-pin revalidation: the slot cannot have been recycled for a
      // different line while our pin is held, so a changed irq means the pin
      // raced a (not yet possible) recycle and must be dropped.
      if (source.irq != record.irq) {
        HAL_ATOMIC_FetchAdd32(
            VirtualAddress::from_ptr(&source.pinState), static_cast<uint32_t>(-1), PlatformMemoryOrder::Release);
        continue;
      }
      const auto status = QueueDpcUnchecked(source.dpc, record);
      HAL_ATOMIC_FetchAdd32(
          VirtualAddress::from_ptr(&source.pinState), static_cast<uint32_t>(-1), PlatformMemoryOrder::Release);
      return status;
    }
    return KernelError::InvalidArgument;// no registered source for this line
  }

  // --- Worker ---------------------------------------------------------------

  // Processes at most one queued DPC. Returns false when the ring is empty.
  // Worker ordering contract: copy the argument snapshot while the pending
  // bit is still set, claim the invocation (pending -> running, release) or
  // consume a cancelled entry, invoke the copied callback snapshot without
  // holding any lock, then complete the interrupt source.
  bool DrainOne() noexcept {
    KDpc *dpc = nullptr;
    if (!m_queue.Pop(dpc)) return false;
    if (!dpc) return true;// defensive: slot already released

    // Record the worker's identity so a disconnect issued from inside one of
    // the callbacks below can be recognized and rejected (single consumer).
    if (m_threadIdFn) m_workerThreadId = m_threadIdFn();

    // Step 2: worker-local copies while kStatePending is still set. The
    // in-flight count is raised under the registry lock, atomically with the
    // snapshot, so UnregisterHandler can wait for these callbacks (rundown).
    const HalInterruptRecord args = dpc->args;
    KObjectPtr<KInterruptServiceRoutineObject> handlers[kMaxHandlersPerInterrupt]{};
    size_t handlerCount = 0;
    const bool interruptSource = dpc->isInterruptSource;
    const uint32_t irq = dpc->sourceIrq;
    Source *src = nullptr;
    if (interruptSource) {
      kstd::unique_lock guard(m_registryLock);
      for (auto &source: m_sources) {
        if (&source.dpc != dpc) continue;
        src = &source;
        if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire) & kSourceActiveBit) {
          handlerCount = source.count;
          for (size_t i = 0; i < handlerCount; ++i) handlers[i] = source.handlers[i];
        }
        HAL_ATOMIC_FetchAdd32(
            VirtualAddress::from_ptr(&source.inFlight), 1, PlatformMemoryOrder::Acquire);
        break;
      }
    }

    // Step 3: start claim -- the callback-start linearization point.
    bool run = false;
    for (;;) {
      uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc->state), PlatformMemoryOrder::Acquire);
      if (!(state & KDpc::kStatePending)) break;// invariant breach; skip defensively
      if (state & KDpc::kStateCancelled) {
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&dpc->state), state,
                state & ~(KDpc::kStatePending | KDpc::kStateCancelled),
                PlatformMemoryOrder::AcquireRelease)) {
          break;// cancelled entry consumed; callbacks skipped
        }
      } else if (HAL_ATOMIC_CompareExchange32(
                     VirtualAddress::from_ptr(&dpc->state), state,
                     (state & ~KDpc::kStatePending) | KDpc::kStateRunning,
                     PlatformMemoryOrder::AcquireRelease)) {
        run = true;
        break;
      }
    }

    // Step 4: invoke the copied snapshot without holding any lock. A producer
    // may queue the next invocation while these run; it cannot overwrite the
    // worker-local copies.
    if (run) {
      if (interruptSource) {
        for (size_t i = 0; i < handlerCount; ++i) {
          if (handlers[i] && handlers[i]->service_routine) {
            handlers[i]->service_routine(handlers[i], handlers[i]->service_context);
          }
        }
      } else if (dpc->routine) {
        dpc->routine(*dpc, dpc->context, args);
      }
      // Clear kStateRunning, preserving any later kStatePending.
      for (;;) {
        uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc->state), PlatformMemoryOrder::Relaxed);
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&dpc->state), state,
                state & ~KDpc::kStateRunning, PlatformMemoryOrder::Release)) {
          break;
        }
      }
    }

    // The snapshot's callbacks have returned: drop the in-flight count so a
    // waiting disconnect can proceed (its borrowed service_context is no
    // longer referenced by this worker).
    if (src) {
      HAL_ATOMIC_FetchAdd32(
          VirtualAddress::from_ptr(&src->inFlight), static_cast<uint32_t>(-1), PlatformMemoryOrder::Release);
    }

    // Step 5: complete the source only after the whole dequeue snapshot has
    // returned (including cancellation cleanup). Unmasks a level line;
    // no-op for edge. Worker-local copies release here (end of scope).
    if (interruptSource) {
      HAL_INTR_CompleteInterrupt(irq);
    }

    // Deferred reservation release: this consume may have been the stale
    // queued pointer of an already-disconnected source.
    if (src) {
      kstd::unique_lock guard(m_registryLock);
      TryReleaseSourceReservationLocked(*src);
    }
    return true;
  }

  [[nodiscard]] bool QueueEmpty() noexcept { return m_queue.Empty(); }

  [[nodiscard]] uint32_t QueueFullDiagnostics() noexcept {
    return HAL_ATOMIC_Load32(
        VirtualAddress::from_ptr(&m_queueFullDiagnostics), PlatformMemoryOrder::Relaxed);
  }

  [[nodiscard]] uint32_t ReservedCount() noexcept {
    kstd::unique_lock guard(m_registryLock);
    return m_reserved;
  }

 private:
  // Requires m_registryLock. Releases a torn-down source's deferred queue
  // reservation once the source is inactive, its producer pins have drained,
  // and its queued/in-flight DPC work has completed.
  void TryReleaseSourceReservationLocked(Source &source) noexcept {
    if (source.pendingRelease == 0) return;
    if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.pinState), PlatformMemoryOrder::Acquire) != 0) return;
    if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.dpc.state), PlatformMemoryOrder::Acquire) != 0) return;
    if (HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&source.inFlight), PlatformMemoryOrder::Acquire) != 0) return;
    source.pendingRelease = 0;
    --m_reserved;
  }

  // Same as QueueDpc but trusts the system-owned source DPC's registration,
  // avoiding a racy `registered` read from the IRQ gate.
  kstd::error_code QueueDpcUnchecked(KDpc &dpc, const HalInterruptRecord &args) noexcept {
    for (;;) {
      uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Relaxed);
      if (state & KDpc::kStatePending) return {};
      if (HAL_ATOMIC_CompareExchange32(
              VirtualAddress::from_ptr(&dpc.state), state,
              state | KDpc::kStatePending, PlatformMemoryOrder::Acquire)) {
        break;
      }
    }
    dpc.args = args;
    if (!m_queue.Push(&dpc)) {
      HAL_ATOMIC_FetchAdd32(
          VirtualAddress::from_ptr(&m_queueFullDiagnostics), 1, PlatformMemoryOrder::Relaxed);
      for (;;) {
        uint32_t state = HAL_ATOMIC_Load32(VirtualAddress::from_ptr(&dpc.state), PlatformMemoryOrder::Relaxed);
        if (HAL_ATOMIC_CompareExchange32(
                VirtualAddress::from_ptr(&dpc.state), state,
                state & ~(KDpc::kStatePending | KDpc::kStateCancelled),
                PlatformMemoryOrder::Release)) {
          break;
        }
      }
      return KernelError::QueueFull;
    }
    return {};
  }

  DpcQueue m_queue{};
  Source m_sources[kMaxSources]{};
  Mutex m_registryLock{};
  uint32_t m_reserved{0};
  uint32_t m_reservationLimit{kQueueCapacity};
  uint32_t m_queueFullDiagnostics{0};
  void *(*m_threadIdFn)() noexcept {nullptr};
  void *m_workerThreadId{nullptr};// single consumer; written only by the worker
};

DpcCore &core() noexcept;

} // namespace kernel::dispatch
