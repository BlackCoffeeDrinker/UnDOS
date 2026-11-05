#include <catch2/catch_test_macros.hpp>
#include <kernel/dispatch.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "dispatch.hpp"
#include "dispatch_test_hooks.hpp"
#include "ob_test_support.hpp"

namespace {
using kernel::DpcQueue;
using kernel::HalInterruptRecord;
using kernel::KDpc;
using kernel::dispatch::DpcCore;
}// namespace

TEST_CASE("DpcQueue basic FIFO, full detection and slot reuse across wrap", "[dispatch][dpc]") {
  auto queue = std::make_unique<DpcQueue>();
  queue->Reset();
  REQUIRE(queue->Empty());

  KDpc items[DpcQueue::kCapacity + 1]{};
  KDpc *out = nullptr;
  REQUIRE_FALSE(queue->Pop(out));

  // All 256 slots are usable -- no head+1==tail sentinel.
  for (uint32_t i = 0; i < DpcQueue::kCapacity; ++i) {
    REQUIRE(queue->Push(&items[i]));
  }
  REQUIRE_FALSE(queue->Push(&items[DpcQueue::kCapacity]));// full, no corruption

  for (uint32_t i = 0; i < DpcQueue::kCapacity; ++i) {
    REQUIRE(queue->Pop(out));
    CHECK(out == &items[i]);
  }
  REQUIRE_FALSE(queue->Pop(out));
  CHECK(queue->Empty());

  // Drive the positions through many generations (exercises per-slot sequence
  // reuse and 32-bit position arithmetic far past the capacity).
  for (uint32_t round = 0; round < 10'000; ++round) {
    REQUIRE(queue->Push(&items[round % DpcQueue::kCapacity]));
    REQUIRE(queue->Pop(out));
    CHECK(out == &items[round % DpcQueue::kCapacity]);
  }
  CHECK(queue->Empty());
}

TEST_CASE("DpcQueue multi-producer stress loses and duplicates nothing", "[dispatch][dpc]") {
  auto queue = std::make_unique<DpcQueue>();
  queue->Reset();

  constexpr uint32_t kProducers = 4;
  constexpr uint32_t kPerProducer = 20'000;
  // Each producer owns a disjoint pool of KDpc objects; a pointer is only
  // re-pushed after the consumer has observed it, mirroring the one-entry-per
  // registered-DPC invariant.
  std::vector<std::vector<KDpc>> pools(kProducers);
  for (auto &pool: pools) pool.resize(DpcQueue::kCapacity / kProducers);
  std::vector<std::atomic<uint32_t>> consumedPerProducer(kProducers);
  for (auto &c: consumedPerProducer) c.store(0);
  // Payload check: stash (producer << 16 | seq) in the vector field and
  // verify every value is seen exactly once per slot reuse (no torn reads).
  std::vector<std::vector<std::atomic<uint32_t>>> released(kProducers);
  for (auto &r: released) {
    r = std::vector<std::atomic<uint32_t>>(DpcQueue::kCapacity / kProducers);
    for (auto &f: r) f.store(1);// 1 == free for the producer to use
  }

  std::atomic<bool> start{false};
  std::atomic<uint32_t> totalConsumed{0};

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (uint32_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      while (!start.load()) {}
      uint32_t sent = 0;
      uint32_t slot = 0;
      while (sent < kPerProducer) {
        auto &pool = pools[p];
        slot = (slot + 1) % pool.size();
        if (!released[p][slot].exchange(0)) {
          released[p][slot].store(0);// was already in flight; keep looking
          std::this_thread::yield();
          continue;
        }
        pool[slot].args.vector = (p << 16) | (sent & 0xFFFF);
        pool[slot].args.irq = p;
        pool[slot].sourceIrq = slot;
        while (!queue->Push(&pool[slot])) {
          std::this_thread::yield();// bounded ring; wait for the consumer
        }
        ++sent;
      }
    });
  }

  // Catch2 assertions are not thread-safe; the consumer records failures and
  // the main thread asserts after join.
  std::atomic<uint32_t> nullPops{0};
  std::atomic<uint32_t> tornPayloads{0};
  std::thread consumer([&] {
    while (!start.load()) {}
    uint32_t seen = 0;
    while (seen < kProducers * kPerProducer) {
      KDpc *out = nullptr;
      if (!queue->Pop(out)) {
        std::this_thread::yield();
        continue;
      }
      if (out == nullptr) {
        nullPops.fetch_add(1);
        ++seen;
        continue;
      }
      const uint32_t producer = out->args.irq;
      // Payload is not torn: vector's high half must match the producer.
      if (producer >= kProducers || (out->args.vector >> 16) != producer) {
        tornPayloads.fetch_add(1);
        ++seen;
        continue;
      }
      consumedPerProducer[producer].fetch_add(1);
      // Hand the object back to its producer.
      released[producer][out->sourceIrq].store(1);
      ++seen;
    }
    totalConsumed.store(seen);
  });

  start.store(true);
  for (auto &t: producers) t.join();
  consumer.join();

  CHECK(totalConsumed.load() == kProducers * kPerProducer);
  CHECK(nullPops.load() == 0);
  CHECK(tornPayloads.load() == 0);
  for (uint32_t p = 0; p < kProducers; ++p) {
    CHECK(consumedPerProducer[p].load() == kPerProducer);
  }
}

TEST_CASE("KDpc first-wins argument capture and coalescing", "[dispatch][dpc]") {
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  struct Seen {
    uint32_t count{0};
    HalInterruptRecord last{};
  } seen;

  KDpc dpc{};
  dpc.routine = kernel::KDpcRoutine(
      [](KDpc &, void *ctx, const HalInterruptRecord &args) {
        auto *s = static_cast<Seen *>(ctx);
        s->count++;
        s->last = args;
      });
  dpc.context = &seen;

  REQUIRE_FALSE(core.RegisterDpc(dpc));

  // First producer wins and owns the snapshot; the second coalesces and its
  // arguments are discarded.
  REQUIRE_FALSE(core.QueueDpc(dpc, {.vector = 1, .irq = 7, .timestamp = 111, .flags = 0}));
  REQUIRE_FALSE(core.QueueDpc(dpc, {.vector = 2, .irq = 8, .timestamp = 222, .flags = 0}));

  REQUIRE(core.DrainOne());
  REQUIRE_FALSE(core.DrainOne());// exactly one queue entry
  CHECK(seen.count == 1);
  CHECK(seen.last.vector == 1);
  CHECK(seen.last.irq == 7);
  CHECK(seen.last.timestamp == 111);

  // After consumption the DPC can be queued again with fresh arguments.
  REQUIRE_FALSE(core.QueueDpc(dpc, {.vector = 3, .irq = 9, .timestamp = 333, .flags = 0}));
  REQUIRE(core.DrainOne());
  CHECK(seen.count == 2);
  CHECK(seen.last.vector == 3);

  REQUIRE_FALSE(core.UnregisterDpc(dpc));
  test_dispatch::Reset();
}

TEST_CASE("KDpc requeue from within the running callback", "[dispatch][dpc]") {
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  struct Ctx {
    DpcCore *core;
    KDpc *dpc;
    uint32_t count{0};
  } ctx{&core, nullptr, 0};

  KDpc dpc{};
  ctx.dpc = &dpc;
  dpc.routine = kernel::KDpcRoutine(
      [](KDpc &, void *opaque, const HalInterruptRecord &) {
        auto *c = static_cast<Ctx *>(opaque);
        c->count++;
        if (c->count == 1) {
          // The running invocation cleared PENDING at its start claim, so a
          // new event may capture one subsequent run while this one executes.
          REQUIRE_FALSE(c->core->QueueDpc(*c->dpc, {.vector = 42, .irq = 0, .timestamp = 0, .flags = 0}));
        }
      });
  dpc.context = &ctx;

  REQUIRE_FALSE(core.RegisterDpc(dpc));
  REQUIRE_FALSE(core.QueueDpc(dpc, {}));
  REQUIRE(core.DrainOne());// first run queues the second
  CHECK(ctx.count == 1);
  REQUIRE(core.DrainOne());// second run
  CHECK(ctx.count == 2);
  REQUIRE_FALSE(core.DrainOne());

  REQUIRE_FALSE(core.UnregisterDpc(dpc));
  test_dispatch::Reset();
}

TEST_CASE("KE_DPC_Cancel logical cancellation semantics", "[dispatch][dpc]") {
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  uint32_t calls = 0;
  KDpc dpc{};
  dpc.routine = kernel::KDpcRoutine(
      [](KDpc &, void *ctx, const HalInterruptRecord &) { ++*static_cast<uint32_t *>(ctx); });
  dpc.context = &calls;
  REQUIRE_FALSE(core.RegisterDpc(dpc));

  // Nothing pending: cancel returns false.
  CHECK_FALSE(core.CancelDpc(dpc));

  // Cancel wins before the worker's start claim: callback skipped; the
  // cancelled pointer is still consumed and releases its ring slot.
  REQUIRE_FALSE(core.QueueDpc(dpc, {}));
  CHECK(core.CancelDpc(dpc));
  CHECK_FALSE(core.CancelDpc(dpc));// already cancelled
  // Requeue is not possible while the cancelled pointer is outstanding.
  REQUIRE_FALSE(core.QueueDpc(dpc, {}));// coalesces into the cancelled entry
  REQUIRE(core.DrainOne());// consumes the cancelled pointer
  CHECK(calls == 0);
  CHECK((dpc.state & (KDpc::kStatePending | KDpc::kStateCancelled)) == 0);

  // After consumption a new invocation queues and runs normally.
  REQUIRE_FALSE(core.QueueDpc(dpc, {}));
  REQUIRE(core.DrainOne());
  CHECK(calls == 1);

  // Once the invocation ran, cancel is too late.
  CHECK_FALSE(core.CancelDpc(dpc));

  REQUIRE_FALSE(core.UnregisterDpc(dpc));
  test_dispatch::Reset();
}

TEST_CASE("KE_DPC_Cancel races the worker start claim without losing entries", "[dispatch][dpc]") {
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  std::atomic<uint32_t> calls{0};
  KDpc dpc{};
  dpc.routine = kernel::KDpcRoutine(
      [](KDpc &, void *ctx, const HalInterruptRecord &) {
        static_cast<std::atomic<uint32_t> *>(ctx)->fetch_add(1);
      });
  dpc.context = &calls;
  REQUIRE_FALSE(core.RegisterDpc(dpc));

  constexpr uint32_t kRounds = 20'000;
  uint32_t cancelled = 0;
  for (uint32_t i = 0; i < kRounds; ++i) {
    REQUIRE_FALSE(core.QueueDpc(dpc, {}));
    std::atomic<bool> go{false};
    bool cancelWon = false;
    bool drained = false;
    std::thread canceller([&] {
      while (!go.load()) {}
      cancelWon = core.CancelDpc(dpc);
    });
    std::thread worker([&] {
      while (!go.load()) {}
      drained = core.DrainOne();
    });
    go.store(true);
    canceller.join();
    worker.join();
    REQUIRE(drained);
    if (cancelWon) ++cancelled;
    // Exactly one outcome per round: either the callback ran or it was
    // skipped due to a winning cancellation. The entry is always consumed.
    CHECK((dpc.state & (KDpc::kStatePending | KDpc::kStateCancelled | KDpc::kStateRunning)) == 0);
  }
  CHECK(calls.load() + cancelled == kRounds);

  REQUIRE_FALSE(core.UnregisterDpc(dpc));
  test_dispatch::Reset();
}

TEST_CASE("DPC registration reserves capacity and enforces the limit", "[dispatch][dpc]") {
  // Private core so the global one keeps its reservations intact.
  auto core = std::make_unique<DpcCore>();
  std::vector<KDpc> dpcs(DpcCore::kQueueCapacity + 1);

  for (uint32_t i = 0; i < DpcCore::kQueueCapacity; ++i) {
    REQUIRE_FALSE(core->RegisterDpc(dpcs[i]));
  }
  CHECK(core->ReservedCount() == DpcCore::kQueueCapacity);
  // The reservation budget is exhausted: registration fails up front.
  CHECK(core->RegisterDpc(dpcs[DpcCore::kQueueCapacity]) == kernel::KernelError::ResourceLimitExceeded);
  // Queueing an unregistered DPC is invalid.
  CHECK(core->QueueDpc(dpcs[DpcCore::kQueueCapacity], {}) == kernel::KernelError::InvalidArgument);

  // Every registered DPC can be queued: reservation guarantees ring capacity.
  for (uint32_t i = 0; i < DpcCore::kQueueCapacity; ++i) {
    REQUIRE_FALSE(core->QueueDpc(dpcs[i], {}));
  }
  CHECK(core->QueueFullDiagnostics() == 0);

  // Unregister fails while an invocation is queued (rundown), succeeds after.
  CHECK(core->UnregisterDpc(dpcs[0]) == kernel::KernelError::ResourceConflict);
  while (core->DrainOne()) {}
  REQUIRE_FALSE(core->UnregisterDpc(dpcs[0]));
  CHECK(core->ReservedCount() == DpcCore::kQueueCapacity - 1);
}

TEST_CASE("Unexpected queue overflow records a diagnostic and clears the queued state", "[dispatch][dpc]") {
  // Over-provisioned reservation limit: test-only seam to reach the
  // "unexpected full" path that reservation makes unreachable in production.
  auto core = std::make_unique<DpcCore>(DpcCore::kQueueCapacity + 1);
  std::vector<KDpc> dpcs(DpcCore::kQueueCapacity + 1);
  for (auto &dpc: dpcs) REQUIRE_FALSE(core->RegisterDpc(dpc));
  for (uint32_t i = 0; i < DpcCore::kQueueCapacity; ++i) {
    REQUIRE_FALSE(core->QueueDpc(dpcs[i], {}));
  }

  auto &overflow = dpcs[DpcCore::kQueueCapacity];
  CHECK(core->QueueDpc(overflow, {}) == kernel::KernelError::QueueFull);
  CHECK(core->QueueFullDiagnostics() == 1);
  // The DPC is not stuck: its queued state was cleared, so it can be queued
  // again once capacity exists. The callback was never invoked inline.
  CHECK((overflow.state & KDpc::kStatePending) == 0);
  while (core->DrainOne()) {}
  REQUIRE_FALSE(core->QueueDpc(overflow, {}));
  REQUIRE(core->DrainOne());
}

TEST_CASE("Level-triggered source is completed only after the callback snapshot", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr, void *) {
    test_dispatch::RecordCallbackEvent(isr->vector);
  };
  auto isr = KE_IO_ConnectInterrupt(
      nullptr, 11,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      nullptr);
  REQUIRE(isr != nullptr);
  (*isr)->vector = 11;

  // Level occurrence: HAL masked then EOI'd in the gate; the dispatcher must
  // call HAL_INTR_CompleteInterrupt(irq) only after the snapshot returns.
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt(
      {.vector = 43, .irq = 11, .timestamp = 1, .flags = kernel::kHalInterruptRecordLevelTriggered}));
  test_dispatch::Pump();

  const auto &events = test_dispatch::FakeHal().events;
  REQUIRE(events.size() == 2);
  CHECK(events[0].kind == test_dispatch::FakeHalEvent::Callback);
  CHECK(events[1].kind == test_dispatch::FakeHalEvent::Complete);
  CHECK(events[1].irq == 11);
  CHECK(test_dispatch::FakeHal().completeCount == 1);

  KE_IO_DisconnectInterrupt(11, *isr);
  test_dispatch::Reset();
}

TEST_CASE("MPSC enqueue of interrupt records from concurrent producers", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  struct Counter {
    std::atomic<uint32_t> calls{0};
  } counter;

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *ctx) {
    static_cast<Counter *>(ctx)->calls.fetch_add(1);
  };
  auto isr = KE_IO_ConnectInterrupt(
      nullptr, 3,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      &counter);
  REQUIRE(isr != nullptr);

  // Multiple producers hammer the same line while the worker drains: no
  // deadlock, no stuck pending state, and the worker never sees a torn entry.
  constexpr uint32_t kProducers = 4;
  constexpr uint32_t kPerProducer = 10'000;
  std::atomic<bool> stop{false};
  std::thread worker([&] {
    while (!stop.load()) {
      while (core.DrainOne()) {}
      std::this_thread::yield();
    }
    while (core.DrainOne()) {}
  });

  std::vector<std::thread> producers;
  for (uint32_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (uint32_t i = 0; i < kPerProducer; ++i) {
        (void) KE_DISPATCH_EnqueueInterrupt({.vector = 35, .irq = 3, .timestamp = i, .flags = 0});
      }
      (void) p;
    });
  }
  for (auto &t: producers) t.join();
  stop.store(true);
  worker.join();

  // Coalescing makes the exact count nondeterministic, but at least one
  // callback must have run, none may run after a final drain with the queue
  // empty, and the DPC must be idle (not stuck pending/running).
  CHECK(counter.calls.load() >= 1);
  CHECK(counter.calls.load() <= kProducers * kPerProducer);
  CHECK(core.QueueFullDiagnostics() == 0);
  REQUIRE_FALSE(core.DrainOne());

  KE_IO_DisconnectInterrupt(3, *isr);
  test_dispatch::Reset();
}

TEST_CASE("Handler disconnect waits for the in-flight callback (rundown)", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  struct Ctx {
    std::atomic<bool> inCallback{false};
    std::atomic<bool> release{false};
    std::atomic<bool> callbackDone{false};
  } ctx;

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *opaque) {
    auto *c = static_cast<Ctx *>(opaque);
    c->inCallback.store(true);
    while (!c->release.load()) std::this_thread::yield();
    c->callbackDone.store(true);
  };
  auto isr = KE_IO_ConnectInterrupt(
      nullptr, 4,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      &ctx);
  REQUIRE(isr != nullptr);

  REQUIRE_FALSE(KE_DISPATCH_EnqueueInterrupt({.vector = 36, .irq = 4, .timestamp = 1, .flags = 0}));

  // Worker blocks inside the callback (a driver callback may wait on a mutex).
  std::thread worker([&] { core.DrainOne(); });
  while (!ctx.inCallback.load()) std::this_thread::yield();

  // Disconnect must not return while the snapshot still runs this handler:
  // the driver frees its borrowed service_context right after it returns.
  std::atomic<bool> disconnectReturned{false};
  std::atomic<bool> sawCallbackDoneAtReturn{false};
  std::thread disconnector([&] {
    KE_IO_DisconnectInterrupt(4, *isr);
    sawCallbackDoneAtReturn.store(ctx.callbackDone.load());
    disconnectReturned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK_FALSE(disconnectReturned.load());// still waiting on the rundown

  ctx.release.store(true);
  disconnector.join();
  worker.join();
  CHECK(sawCallbackDoneAtReturn.load());// callback finished before return
  CHECK(KE_DISPATCH_HandlerCount(4) == 0);

  test_dispatch::Reset();
}

TEST_CASE("Disconnect from the handler's own callback is rejected, not deadlocked", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  struct Ctx {
    DpcCore *core{nullptr};
    kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> isr;
    kstd::error_code status{};
    bool ran{false};
  } ctx;
  ctx.core = &core;

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *opaque) {
    auto *c = static_cast<Ctx *>(opaque);
    // Synchronous disconnect from the dispatch worker's own invocation would
    // self-deadlock on the rundown wait; the registry must reject it.
    c->status = c->core->UnregisterHandler(10, c->isr);
    c->ran = true;
  };
  auto isr = KE_IO_ConnectInterrupt(
      nullptr, 10,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      &ctx);
  REQUIRE(isr != nullptr);
  ctx.isr = *isr;

  REQUIRE_FALSE(KE_DISPATCH_EnqueueInterrupt({.vector = 42, .irq = 10, .timestamp = 1, .flags = 0}));
  test_dispatch::Pump();// would hang here if the disconnect waited on itself

  CHECK(ctx.ran);
  CHECK(ctx.status == kernel::KernelError::ResourceConflict);
  CHECK(KE_DISPATCH_HandlerCount(10) == 1);// registration stays intact

  ctx.isr = nullptr;
  KE_IO_DisconnectInterrupt(10, *isr);// legal from outside the callback
  CHECK(KE_DISPATCH_HandlerCount(10) == 0);
  test_dispatch::Reset();
}

TEST_CASE("Concurrent first registrations connect the HAL source exactly once", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *) {};
  const kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)> fn(routine);

  // Pre-create the ISR objects serially (the host object manager is not part
  // of this race); each is parked on its own temporary line first.
  constexpr uint32_t kThreads = 4;// == kMaxHandlersPerInterrupt
  std::vector<kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject>> isrs(kThreads);
  for (uint32_t t = 0; t < kThreads; ++t) {
    auto isr = KE_IO_ConnectInterrupt(nullptr, 20 + t, fn, nullptr);
    REQUIRE(isr != nullptr);
    isrs[t] = *isr;
    KE_IO_DisconnectInterrupt(20 + t, isrs[t]);
  }
  test_dispatch::FakeHal() = test_dispatch::FakeHalState{};// count only the race

  // Four racers register on the same fresh line: the first/last decision is
  // made under the registry lock, so exactly one HAL connect may happen no
  // matter the interleaving (the old HandlerCount()==1 recheck could do zero).
  std::atomic<bool> go{false};
  std::atomic<uint32_t> failures{0};
  std::vector<std::thread> threads;
  for (uint32_t t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!go.load()) {}
      if (KE_DISPATCH_RegisterHandler(6, isrs[t])) failures.fetch_add(1);
    });
  }
  go.store(true);
  for (auto &t: threads) t.join();

  CHECK(failures.load() == 0);
  CHECK(test_dispatch::FakeHal().connectCount == 1);
  CHECK(KE_DISPATCH_HandlerCount(6) == kThreads);

  // Concurrent disconnects: exactly one HAL disconnect, by the last leaver.
  threads.clear();
  go.store(false);
  for (uint32_t t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!go.load()) {}
      if (KE_DISPATCH_UnregisterHandler(6, isrs[t])) failures.fetch_add(1);
    });
  }
  go.store(true);
  for (auto &t: threads) t.join();

  CHECK(failures.load() == 0);
  CHECK(test_dispatch::FakeHal().disconnectCount == 1);
  CHECK(KE_DISPATCH_HandlerCount(6) == 0);
  test_dispatch::Reset();
}

TEST_CASE("Source reservation release is deferred while its queued pointer is outstanding", "[dispatch][dpc]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();
  auto &core = test_dispatch::Core();

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *) {};
  auto isr = KE_IO_ConnectInterrupt(
      nullptr, 7,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      nullptr);
  REQUIRE(isr != nullptr);
  CHECK(core.ReservedCount() == 1);

  // Leave the source's pointer in the ring, then remove the last handler.
  REQUIRE_FALSE(KE_DISPATCH_EnqueueInterrupt({.vector = 39, .irq = 7, .timestamp = 1, .flags = 0}));
  KE_IO_DisconnectInterrupt(7, *isr);
  CHECK(KE_DISPATCH_HandlerCount(7) == 0);
  // The stale pointer still occupies a ring slot, so the reservation count
  // must still cover it (releasing it here could let registrations exceed
  // the ring and reach the "unreachable" queue-full path).
  CHECK(core.ReservedCount() == 1);

  // A fresh registration takes a different clean slot and its own reservation.
  auto isr2 = KE_IO_ConnectInterrupt(
      nullptr, 7,
      kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(routine),
      nullptr);
  REQUIRE(isr2 != nullptr);
  CHECK(core.ReservedCount() == 2);

  // Consuming the stale pointer releases the deferred reservation.
  test_dispatch::Pump();
  CHECK(core.ReservedCount() == 1);
  CHECK(core.QueueFullDiagnostics() == 0);

  KE_IO_DisconnectInterrupt(7, *isr2);
  CHECK(core.ReservedCount() == 0);
  test_dispatch::Reset();
}
