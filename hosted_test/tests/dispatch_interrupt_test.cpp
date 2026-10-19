#include <catch2/catch_test_macros.hpp>
#include <kernel/dispatch.hpp>
#include <kernel/io.hpp>
#include <vector>

#include "dispatch_test_hooks.hpp"
#include "ob_test_support.hpp"

TEST_CASE("InterruptRecordRing basic operations", "[dispatch]") {
  kernel::InterruptRecordRing ring;

  REQUIRE(ring.Empty());
  REQUIRE(ring.Size() == 0);

  kernel::HalInterruptRecord rec{};
  REQUIRE_FALSE(ring.Pop(rec));

  kernel::HalInterruptRecord in1{.vector = 33, .irq = 1, .timestamp = 1000, .flags = 0};
  REQUIRE(ring.Push(in1) == kernel::KernelError::Success);
  REQUIRE_FALSE(ring.Empty());
  REQUIRE(ring.Size() == 1);

  kernel::HalInterruptRecord in2{.vector = 44, .irq = 12, .timestamp = 2000, .flags = 1};
  REQUIRE(ring.Push(in2) == kernel::KernelError::Success);
  REQUIRE(ring.Size() == 2);

  kernel::HalInterruptRecord out{};
  REQUIRE(ring.Pop(out));
  CHECK(out.vector == 33);
  CHECK(out.irq == 1);
  CHECK(out.timestamp == 1000);
  CHECK(out.flags == 0);

  REQUIRE(ring.Pop(out));
  CHECK(out.vector == 44);
  CHECK(out.irq == 12);
  CHECK(out.timestamp == 2000);
  CHECK(out.flags == 1);

  REQUIRE(ring.Empty());
  REQUIRE(ring.Size() == 0);
  REQUIRE_FALSE(ring.Pop(out));
}

TEST_CASE("InterruptRecordRing capacity and overflow handling", "[dispatch]") {
  kernel::InterruptRecordRing ring;

  // Capacity is 256. Ring buffer uses (head + 1) % capacity == tail as full condition,
  // which holds (kCapacity - 1) = 255 elements.
  size_t pushedCount = 0;
  for (size_t i = 0; i < kernel::InterruptRecordRing::kCapacity; ++i) {
    kernel::HalInterruptRecord rec{.vector = static_cast<uint32_t>(32 + i % 16), .irq = static_cast<uint32_t>(i % 16), .timestamp = i, .flags = 0};
    if (ring.Push(rec) == kernel::KernelError::Success) {
      pushedCount++;
    } else {
      break;
    }
  }

  CHECK(pushedCount == kernel::InterruptRecordRing::kCapacity - 1);
  CHECK(ring.Size() == pushedCount);

  // Subsequent pushes must fail gracefully without crash
  kernel::HalInterruptRecord overflowRec{.vector = 99, .irq = 15, .timestamp = 9999, .flags = 0};
  CHECK(ring.Push(overflowRec) == kernel::KernelError::QueueFull);

  // Drain all elements and verify FIFO integrity
  size_t poppedCount = 0;
  kernel::HalInterruptRecord out{};
  while (ring.Pop(out)) {
    CHECK(out.timestamp == poppedCount);
    CHECK(out.irq == poppedCount % 16);
    poppedCount++;
  }
  CHECK(poppedCount == pushedCount);
  CHECK(ring.Empty());

  // Ring can be reused after draining
  CHECK(ring.Push(overflowRec) == kernel::KernelError::Success);
  CHECK(ring.Pop(out));
  CHECK(out.vector == 99);
}

TEST_CASE("InterruptRecordRing reset", "[dispatch]") {
  kernel::InterruptRecordRing ring;
  ring.Push({.vector = 33, .irq = 1, .timestamp = 100, .flags = 0});
  ring.Push({.vector = 34, .irq = 2, .timestamp = 200, .flags = 0});
  REQUIRE(ring.Size() == 2);

  ring.Reset();
  CHECK(ring.Empty());
  CHECK(ring.Size() == 0);
  kernel::HalInterruptRecord out{};
  CHECK_FALSE(ring.Pop(out));
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
  auto kbdIsr = KE_IO_ConnectInterrupt(
      nullptr, 1, kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(kbdRoutine), &kbdCtx);
  auto mouseIsr = KE_IO_ConnectInterrupt(
      nullptr, 12, kernel::cfunc<void(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *)>(mouseRoutine), &mouseCtx);

  REQUIRE(kbdIsr != nullptr);
  REQUIRE(mouseIsr != nullptr);

  // Enqueue interleaved interrupts: IRQ 1, IRQ 12, IRQ 1, unregistered IRQ 7
  CHECK(KE_DISPATCH_EnqueueInterrupt({33, 1, 10, 0}) == kernel::KernelError::Success);
  CHECK(KE_DISPATCH_EnqueueInterrupt({44, 12, 20, 0}) == kernel::KernelError::Success);
  CHECK(KE_DISPATCH_EnqueueInterrupt({33, 1, 30, 0}) == kernel::KernelError::Success);
  CHECK(KE_DISPATCH_EnqueueInterrupt({39, 7, 40, 0}) == kernel::KernelError::Success); // Unregistered IRQ

  // Before pump, handlers have not run
  CHECK(kbdCtx.callCount == 0);
  CHECK(mouseCtx.callCount == 0);

  // Pump queue
  test_dispatch::Pump();

  // Handlers should have been invoked
  CHECK(kbdCtx.callCount == 2);
  CHECK(kbdCtx.lastIrq == 1);
  CHECK(mouseCtx.callCount == 1);
  CHECK(mouseCtx.lastIrq == 12);

  // Disconnect keyboard interrupt
  KE_IO_DisconnectInterrupt(1);

  // Enqueue new interrupts for IRQ 1 and IRQ 12
  CHECK(KE_DISPATCH_EnqueueInterrupt({33, 1, 50, 0}) == kernel::KernelError::Success);
  CHECK(KE_DISPATCH_EnqueueInterrupt({44, 12, 60, 0}) == kernel::KernelError::Success);

  test_dispatch::Pump();

  // Keyboard handler was disconnected, callCount should remain 2
  CHECK(kbdCtx.callCount == 2);
  // Mouse handler was still connected, callCount should become 2
  CHECK(mouseCtx.callCount == 2);

  KE_IO_DisconnectInterrupt(12);
  test_dispatch::Reset();
}

TEST_CASE("Dispatch invalid registration parameters", "[dispatch]") {
  EnsureObjectManagerInitialized();
  test_dispatch::Reset();

  // IRQ >= 16 out of bounds
  CHECK(KE_DISPATCH_RegisterHandler(16, nullptr) == kernel::KernelError::InvalidArgument);
  CHECK(KE_DISPATCH_RegisterHandler(32, nullptr) == kernel::KernelError::InvalidArgument);

  // Connect with null routine
  auto nullIsr = KE_IO_ConnectInterrupt(nullptr, 1, nullptr, nullptr);
  CHECK(nullIsr == nullptr);

  test_dispatch::Reset();
}
