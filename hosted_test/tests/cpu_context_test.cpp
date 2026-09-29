#include <catch2/catch_test_macros.hpp>

// CpuContext is kernel-private (it embeds the ready queue), so pull it in
// directly the way scheduler_test.cpp pulls in ReadyQueue.
#include "../../kernel/src/scheduler/CpuContext.hpp"

#include "smp_test_hooks.hpp"

using kernel::cpu::contexts;
using kernel::cpu::current_context;
using kernel::cpu::online_count;

namespace {
// Restores the faked CPU id / interrupt flag however a test case exits.
struct SmpFixture {
  SmpFixture() { test_smp::Reset(); }
  ~SmpFixture() { test_smp::Reset(); }
};
}// namespace

TEST_CASE("current_context follows the CPU id the HAL reports", "[cpu_context]") {
  const SmpFixture fixture;

  kernel::cpu::init_contexts();

  REQUIRE(current_context().id == 0);
  REQUIRE(&current_context() == &contexts()[0]);

  test_smp::SetCurrentCpuId(3);
  REQUIRE(current_context().id == 3);
  REQUIRE(&current_context() == &contexts()[3]);
}

TEST_CASE("each CPU sees its own ready queue and current thread", "[cpu_context]") {
  const SmpFixture fixture;

  kernel::cpu::init_contexts();

  kernel::KThreadObject on_cpu0;
  kernel::KThreadObject on_cpu1;
  on_cpu0.priority = 1;
  on_cpu1.priority = 1;

  current_context().ready.enqueue(&on_cpu0);

  test_smp::SetCurrentCpuId(1);
  REQUIRE(current_context().ready.empty());
  current_context().ready.enqueue(&on_cpu1);

  // CPU 1 must not be able to pull CPU 0's thread out of its own queue.
  REQUIRE(current_context().ready.pop_next() == &on_cpu1);
  REQUIRE(current_context().ready.empty());

  test_smp::SetCurrentCpuId(0);
  REQUIRE(current_context().ready.pop_next() == &on_cpu0);
}

TEST_CASE("online_count counts only slots marked online", "[cpu_context]") {
  const SmpFixture fixture;

  kernel::cpu::init_contexts();
  for (size_t i = 0; i < contexts().size(); ++i) {
    contexts()[i].online = false;
  }
  REQUIRE(online_count() == 0);

  contexts()[0].online = true;
  REQUIRE(online_count() == 1);

  contexts()[2].online = true;
  REQUIRE(online_count() == 2);

  contexts()[0].online = false;
  contexts()[2].online = false;
}

TEST_CASE("an out-of-range HAL CPU id is clamped instead of indexing past the array", "[cpu_context]") {
  const SmpFixture fixture;

  test_smp::SetCurrentCpuId(static_cast<uint32_t>(kernel::MAX_CPUS) + 7);
  REQUIRE(kernel::cpu::current_id() == 0);
}

TEST_CASE("lock_irqsave masks interrupts and unlock_irqrestore restores them", "[spinlock]") {
  const SmpFixture fixture;

  kernel::SpinLock lock;

  REQUIRE(test_smp::InterruptsEnabled());
  const uintptr_t state = lock.lock_irqsave();
  REQUIRE_FALSE(test_smp::InterruptsEnabled());
  // The lock really is held.
  REQUIRE_FALSE(lock.try_lock());
  lock.unlock_irqrestore(state);
  REQUIRE(test_smp::InterruptsEnabled());
}

TEST_CASE("lock_irqsave nested inside a disabled region leaves interrupts disabled", "[spinlock]") {
  const SmpFixture fixture;

  kernel::SpinLock outer;
  kernel::SpinLock inner;

  test_smp::SetInterruptsEnabled(false);

  const uintptr_t outer_state = outer.lock_irqsave();
  {
    const kernel::IrqSpinLockGuard guard(inner);
    REQUIRE_FALSE(test_smp::InterruptsEnabled());
  }
  // Leaving the inner section must NOT re-enable interrupts.
  REQUIRE_FALSE(test_smp::InterruptsEnabled());

  outer.unlock_irqrestore(outer_state);
  REQUIRE_FALSE(test_smp::InterruptsEnabled());
}
