#include <catch2/catch_test_macros.hpp>

// The ready-queue policy is HAL-free and lives in the kernel source tree; pull
// it in directly so it can be exercised in the hosted harness.
#include "../../kernel/src/scheduler/ReadyQueue.hpp"

using kernel::KThreadObject;
using kernel::sched::ReadyQueue;

namespace {
// KThreadObject is non-copyable (it embeds an atomic refcount), so it must be
// constructed in place and configured by reference.
void init_thread(KThreadObject &t, uint8_t priority) {
  t.priority = priority;
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
