#include <catch2/catch_test_macros.hpp>

#include <kernel/percpu.hpp>
#include <kernel/spinlock.hpp>

TEST_CASE("SpinLock lock/unlock round-trip", "[spinlock]") {
  kernel::SpinLock lock;

  REQUIRE(lock.try_lock());
  // Already locked: a second try_lock must fail.
  REQUIRE_FALSE(lock.try_lock());
  lock.unlock();

  // Lockable again after unlock.
  REQUIRE(lock.try_lock());
  lock.unlock();

  lock.lock();
  lock.unlock();
}

TEST_CASE("PerCpu current() targets slot 0 on this single-CPU build", "[percpu]") {
  kernel::PerCpu<int> values;

  values.current() = 42;
  REQUIRE(values[0] == 42);
  REQUIRE(values.current() == 42);
}

TEST_CASE("PerCpu operator[] can address every slot", "[percpu]") {
  kernel::PerCpu<int> values;

  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = static_cast<int>(i);
  }

  for (size_t i = 0; i < values.size(); ++i) {
    REQUIRE(values[i] == static_cast<int>(i));
  }
}

TEST_CASE("cpu::current_id returns 0 on this single-CPU build", "[percpu]") {
  REQUIRE(kernel::cpu::current_id() == 0);
}
