#include <catch2/catch_test_macros.hpp>

// The ready-queue policy is HAL-free and lives in the kernel source tree; pull
// it in directly so it can be exercised in the hosted harness.
#include "../../../Kernel/Source/src/scheduler/ReadyQueue.hpp"
#include "../../../Kernel/Source/src/scheduler/KernelStackCleanup.hpp"

#include <vector>

using kernel::KThreadObject;
using kernel::sched::ReadyQueue;

namespace {
// KThreadObject is non-copyable (it embeds an atomic refcount), so it must be
// constructed in place and configured by reference.
void init_thread(KThreadObject &t, uint8_t priority) {
  t.priority = static_cast<kernel::ThreadPriority>(priority);
  t.state = KThreadObject::State::Ready;
}
} // namespace

TEST_CASE("ReadyQueue empty transitions", "[scheduler]") {
  ReadyQueue q;
  REQUIRE(q.empty());
  REQUIRE(q.pop_next() == nullptr);

  KThreadObject a;
  init_thread(a, 1);
  q.enqueue(&a);
  REQUIRE_FALSE(q.empty());

  REQUIRE(q.pop_next() == &a);
  REQUIRE(q.empty());
  REQUIRE(q.pop_next() == nullptr);
}

TEST_CASE("ReadyQueue round-robin FIFO order and cycling", "[scheduler]") {
  ReadyQueue q;
  KThreadObject a, b, c;
  init_thread(a, 1);
  init_thread(b, 1);
  init_thread(c, 1);

  q.enqueue(&a);
  q.enqueue(&b);
  q.enqueue(&c);

  // Same priority: strict FIFO order.
  KThreadObject *first = q.pop_next();
  REQUIRE(first == &a);

  // A fair scheduler re-queues the running thread at the back of its level.
  q.enqueue(first);

  REQUIRE(q.pop_next() == &b);
  q.enqueue(&b);
  REQUIRE(q.pop_next() == &c);
  q.enqueue(&c);

  // After a full rotation we are back to A.
  REQUIRE(q.pop_next() == &a);
}

TEST_CASE("ReadyQueue single thread self-consistency", "[scheduler]") {
  ReadyQueue q;
  KThreadObject a;
  init_thread(a, 2);

  for (int i = 0; i < 5; ++i) {
    q.enqueue(&a);
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.pop_next() == &a);
    REQUIRE(q.empty());
  }
}

TEST_CASE("ReadyQueue highest priority first, round-robin within a level", "[scheduler]") {
  ReadyQueue q;
  KThreadObject low1, low2, high1, high2;
  init_thread(low1, 1);
  init_thread(low2, 1);
  init_thread(high1, 5);
  init_thread(high2, 5);

  // Interleave arrival so ordering can't be an accident of insertion.
  q.enqueue(&low1);
  q.enqueue(&high1);
  q.enqueue(&low2);
  q.enqueue(&high2);

  // Highest priority drains first, FIFO within the level.
  REQUIRE(q.pop_next() == &high1);
  REQUIRE(q.pop_next() == &high2);
  // Then the lower level, still FIFO.
  REQUIRE(q.pop_next() == &low1);
  REQUIRE(q.pop_next() == &low2);
  REQUIRE(q.empty());
}

TEST_CASE("ReadyQueue remove from head, middle and tail", "[scheduler]") {
  KThreadObject a, b, c;
  init_thread(a, 1);
  init_thread(b, 1);
  init_thread(c, 1);

  SECTION("remove head") {
    ReadyQueue q;
    q.enqueue(&a);
    q.enqueue(&b);
    q.enqueue(&c);
    q.remove(&a);
    REQUIRE(q.pop_next() == &b);
    REQUIRE(q.pop_next() == &c);
    REQUIRE(q.empty());
  }

  SECTION("remove middle") {
    ReadyQueue q;
    q.enqueue(&a);
    q.enqueue(&b);
    q.enqueue(&c);
    q.remove(&b);
    REQUIRE(q.pop_next() == &a);
    REQUIRE(q.pop_next() == &c);
    REQUIRE(q.empty());
  }

  SECTION("remove tail") {
    ReadyQueue q;
    q.enqueue(&a);
    q.enqueue(&b);
    q.enqueue(&c);
    q.remove(&c);
    REQUIRE(q.pop_next() == &a);
    REQUIRE(q.pop_next() == &b);
    REQUIRE(q.empty());
  }
}

TEST_CASE("ReadyQueue remove the only element then re-enqueue", "[scheduler]") {
  ReadyQueue q;
  KThreadObject a;
  init_thread(a, 1);

  q.enqueue(&a);
  q.remove(&a);
  REQUIRE(q.empty());
  REQUIRE(q.pop_next() == nullptr);

  // The node must be clean enough to be re-used immediately.
  q.enqueue(&a);
  REQUIRE(q.pop_next() == &a);
  REQUIRE(q.empty());
}

TEST_CASE("ReadyQueue blocked thread is not returned once removed", "[scheduler]") {
  ReadyQueue q;
  KThreadObject a, b;
  init_thread(a, 1);
  init_thread(b, 1);

  q.enqueue(&a);
  q.enqueue(&b);

  // Blocking a thread removes it from the ready queue (as KE_SCHED_Block does).
  b.state = KThreadObject::State::Blocked;
  q.remove(&b);

  REQUIRE(q.pop_next() == &a);
  REQUIRE(q.pop_next() == nullptr);
}

TEST_CASE("Thread stack cleanup unmaps every page and frees only translated frames", "[scheduler]") {
  std::vector<uintptr_t> unmapped;
  std::vector<uintptr_t> freed;
  void *releasedRegion = nullptr;
  const auto base = kernel::VirtualAddress(0x4000);

  const auto status = kernel::sched::detail::cleanup_kernel_stack(
      base, 3, 0x1000,
      [](kernel::VirtualAddress page) { return page.value == 0x5000 ? kernel::PhysicalAddress(0) : kernel::PhysicalAddress(page.value + 0x100000); },
      [&](kernel::VirtualAddress page) { unmapped.push_back(page.value); },
      [&](kernel::PhysicalAddress phys, size_t count) { REQUIRE(count == 1); freed.push_back(phys.value); },
      [&](void *address) { releasedRegion = address; return kernel::KernelError::Success; });

  REQUIRE(status == kernel::KernelError::Success);
  REQUIRE((unmapped == std::vector<uintptr_t>{0x4000, 0x5000, 0x6000}));
  REQUIRE((freed == std::vector<uintptr_t>{0x104000, 0x106000}));
  REQUIRE(releasedRegion == base.as_ptr());
}

TEST_CASE("Thread stack cleanup still reports region-release failure after page cleanup", "[scheduler]") {
  std::vector<uintptr_t> unmapped;
  std::vector<uintptr_t> freed;
  const auto status = kernel::sched::detail::cleanup_kernel_stack(
      kernel::VirtualAddress(0x8000), 2, 0x1000,
      [](kernel::VirtualAddress page) { return kernel::PhysicalAddress(page.value + 0x200000); },
      [&](kernel::VirtualAddress page) { unmapped.push_back(page.value); },
      [&](kernel::PhysicalAddress phys, size_t count) { REQUIRE(count == 1); freed.push_back(phys.value); },
      [](void *) { return kernel::KernelError::DeviceError; });

  REQUIRE(status == kernel::make_error_code(kernel::KernelError::DeviceError));
  REQUIRE((unmapped == std::vector<uintptr_t>{0x8000, 0x9000}));
  REQUIRE((freed == std::vector<uintptr_t>{0x208000, 0x209000}));
}

TEST_CASE("Thread insertion failure triggers stack cleanup and preserves the insertion error", "[scheduler]") {
  bool cleaned = false;
  kstd::error_code cleanupStatus;
  const auto failure = kernel::sched::detail::insert_or_cleanup(
      [] { return kernel::make_error_code(kernel::KernelError::ResourceConflict); },
      [&] {
        cleaned = true;
        cleanupStatus = kernel::KernelError::DeviceError;
      });
  REQUIRE(failure == kernel::make_error_code(kernel::KernelError::ResourceConflict));
  REQUIRE(cleaned);
  REQUIRE(cleanupStatus == kernel::make_error_code(kernel::KernelError::DeviceError));

  cleaned = false;
  const auto success = kernel::sched::detail::insert_or_cleanup(
      [] { return kernel::make_error_code(kernel::KernelError::Success); },
      [&] { cleaned = true; });
  REQUIRE(success == kernel::make_error_code(kernel::KernelError::Success));
  REQUIRE_FALSE(cleaned);
}
