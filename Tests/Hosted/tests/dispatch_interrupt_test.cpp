#include <catch2/catch_test_macros.hpp>
#include <kernel/dispatch.hpp>
#include <kernel/io.hpp>
#include <vector>

#include "dispatch_test_hooks.hpp"
#include "ob_test_support.hpp"

namespace {
using IsrRoutine = kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>;
}

TEST_CASE("Dispatch handler registration lifecycle and dispatch pump", "[dispatch]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  struct DriverContext {
    uint32_t callCount{0};
    uint32_t lastIrq{0};
  };

  DriverContext kbdCtx{};
  DriverContext mouseCtx{};
  DriverContext extendedCtx{};
  constexpr uint32_t extendedInterruptId = 0x12345678;

  auto kbdRoutine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> & /*isr*/, void *ctx) {
    auto *c = static_cast<DriverContext *>(ctx);
    c->callCount++;
    c->lastIrq = 1;
  };

  auto mouseRoutine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> & /*isr*/, void *ctx) {
    auto *c = static_cast<DriverContext *>(ctx);
    c->callCount++;
    c->lastIrq = 12;
  };

  // Connect keyboard on IRQ 1 and mouse on IRQ 12
  auto kbdIsr = KE_IO_ConnectInterrupt(nullptr, 1, IsrRoutine(kbdRoutine), &kbdCtx);
  auto mouseIsr = KE_IO_ConnectInterrupt(nullptr, 12, IsrRoutine(mouseRoutine), &mouseCtx);

  REQUIRE(kbdIsr != nullptr);
  REQUIRE(mouseIsr != nullptr);

  // Enqueue interleaved interrupts: IRQ 1, IRQ 12, IRQ 1 (coalesced), and an
  // unregistered IRQ 7 which has no active source descriptor.
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({33, 1, 10, 0}));
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({44, 12, 20, 0}));
  // Second occurrence on IRQ 1 before the worker ran: first-wins coalescing,
  // still success for the producer.
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({33, 1, 30, 0}));
  CHECK(KE_DISPATCH_EnqueueInterrupt({39, 7, 40, 0}) == kernel::KernelError::InvalidArgument);

  // Before pump, handlers have not run
  CHECK(kbdCtx.callCount == 0);
  CHECK(mouseCtx.callCount == 0);

  // Pump queue
  test_dispatch::Pump();

  // Each line had exactly one pending source-DPC invocation; the two IRQ 1
  // occurrences coalesced into a single callback (drivers drain device state).
  CHECK(kbdCtx.callCount == 1);
  CHECK(kbdCtx.lastIrq == 1);
  CHECK(mouseCtx.callCount == 1);
  CHECK(mouseCtx.lastIrq == 12);

  // Disconnect keyboard interrupt
  KE_IO_DisconnectInterrupt(1, *kbdIsr);

  // Enqueue new interrupts for IRQ 1 and IRQ 12
  CHECK(KE_DISPATCH_EnqueueInterrupt({33, 1, 50, 0}) == kernel::KernelError::InvalidArgument);
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({44, 12, 60, 0}));

  test_dispatch::Pump();

  // Keyboard handler was disconnected, callCount should remain 1
  CHECK(kbdCtx.callCount == 1);
  // Mouse handler was still connected, callCount should become 2
  CHECK(mouseCtx.callCount == 2);

  KE_IO_DisconnectInterrupt(12, *mouseIsr);

  auto extendedRoutine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *ctx) {
    auto *c = static_cast<DriverContext *>(ctx);
    c->callCount++;
    c->lastIrq = extendedInterruptId;
  };
  auto extendedIsr = KE_IO_ConnectInterrupt(nullptr, extendedInterruptId, IsrRoutine(extendedRoutine), &extendedCtx);
  REQUIRE(extendedIsr != nullptr);
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({.vector = 0x80, .irq = extendedInterruptId, .timestamp = 70, .flags = 0}));
  test_dispatch::Pump();
  CHECK(extendedCtx.callCount == 1);
  CHECK(extendedCtx.lastIrq == extendedInterruptId);
  KE_IO_DisconnectInterrupt(extendedInterruptId, *extendedIsr);

  test_dispatch::Reset();
}

TEST_CASE("Shared interrupt line registration, snapshot and per-handler disconnect", "[dispatch]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  struct DriverContext {
    uint32_t callCount{0};
  };
  DriverContext a{};
  DriverContext b{};

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *ctx) {
    static_cast<DriverContext *>(ctx)->callCount++;
  };

  // Two drivers share IRQ 5; the HAL source is connected exactly once.
  auto isrA = KE_IO_ConnectInterrupt(nullptr, 5, IsrRoutine(routine), &a);
  auto isrB = KE_IO_ConnectInterrupt(nullptr, 5, IsrRoutine(routine), &b);
  REQUIRE(isrA != nullptr);
  REQUIRE(isrB != nullptr);
  CHECK(test_dispatch::FakeHal().connectCount == 1);
  CHECK(KE_DISPATCH_HandlerCount(5) == 2);

  // One occurrence invokes every handler in the snapshot exactly once.
  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({37, 5, 100, 0}));
  test_dispatch::Pump();
  CHECK(a.callCount == 1);
  CHECK(b.callCount == 1);

  // Removing one registration leaves the other attached and the HAL source up.
  KE_IO_DisconnectInterrupt(5, *isrA);
  CHECK(KE_DISPATCH_HandlerCount(5) == 1);
  CHECK(test_dispatch::FakeHal().disconnectCount == 0);

  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({37, 5, 200, 0}));
  test_dispatch::Pump();
  CHECK(a.callCount == 1);
  CHECK(b.callCount == 2);

  // Final removal disconnects the HAL source.
  KE_IO_DisconnectInterrupt(5, *isrB);
  CHECK(KE_DISPATCH_HandlerCount(5) == 0);
  CHECK(test_dispatch::FakeHal().disconnectCount == 1);
  CHECK(test_dispatch::FakeHal().lastDisconnectedIrq == 5);

  test_dispatch::Reset();
}

TEST_CASE("Failed HAL connection rolls back only the attempted registration", "[dispatch]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  struct DriverContext {
    uint32_t callCount{0};
  };
  DriverContext a{};
  DriverContext b{};

  auto routine = [](const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *ctx) {
    static_cast<DriverContext *>(ctx)->callCount++;
  };

  // First connection on a fresh line fails at the HAL: the attempted handler
  // must be rolled back and the line left empty.
  test_dispatch::FakeHal().failNextConnect = true;
  auto failed = KE_IO_ConnectInterrupt(nullptr, 9, IsrRoutine(routine), &a);
  REQUIRE_FALSE(failed);
  CHECK(failed.error() == kernel::KernelError::ResourceConflict);
  CHECK(KE_DISPATCH_HandlerCount(9) == 0);

  // The line is reusable after the rollback, and a later registration that
  // shares an already-active line never re-enters the HAL (so it cannot
  // unregister the existing handlers -- the old defect).
  auto isrA = KE_IO_ConnectInterrupt(nullptr, 9, IsrRoutine(routine), &a);
  REQUIRE(isrA != nullptr);
  auto isrB = KE_IO_ConnectInterrupt(nullptr, 9, IsrRoutine(routine), &b);
  REQUIRE(isrB != nullptr);
  CHECK(KE_DISPATCH_HandlerCount(9) == 2);

  CHECK_FALSE(KE_DISPATCH_EnqueueInterrupt({41, 9, 300, 0}));
  test_dispatch::Pump();
  CHECK(a.callCount == 1);
  CHECK(b.callCount == 1);

  KE_IO_DisconnectInterrupt(9, *isrA);
  KE_IO_DisconnectInterrupt(9, *isrB);
  test_dispatch::Reset();
}

TEST_CASE("Dispatch invalid registration parameters", "[dispatch]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  // Interrupt identifiers have no kernel-defined range; a null handler remains invalid.
  CHECK(KE_DISPATCH_RegisterHandler(16, nullptr) == kernel::KernelError::InvalidArgument);
  CHECK(KE_DISPATCH_RegisterHandler(0xFFFFFFFF, nullptr) == kernel::KernelError::InvalidArgument);

  // Unregistering something that was never registered is an error, not a wipe.
  CHECK(KE_DISPATCH_UnregisterHandler(16, nullptr) == kernel::KernelError::InvalidArgument);

  // Connect with null routine
  auto nullIsr = KE_IO_ConnectInterrupt(nullptr, 1, nullptr, nullptr);
  CHECK_FALSE(nullIsr);
  CHECK(nullIsr.error() == kernel::KernelError::InvalidArgument);

  test_dispatch::Reset();
}
