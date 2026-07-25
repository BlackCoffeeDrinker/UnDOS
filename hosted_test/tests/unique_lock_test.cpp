#include <mutex.hpp>
#include <type_traits.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

struct FakeMutex {
  int lock_count = 0;
  int unlock_count = 0;
  bool try_lock_result = true;

  void lock() { lock_count++; }
  void unlock() { unlock_count++; }
  bool try_lock() {
    if (try_lock_result) lock_count++;
    return try_lock_result;
  }
};

}// namespace

TEST_CASE("unique_lock is not copyable", "[unique_lock]") {
  REQUIRE_FALSE(kstd::is_copy_constructible<kstd::unique_lock<FakeMutex>>::value);
  REQUIRE_FALSE(kstd::is_copy_assignable<kstd::unique_lock<FakeMutex>>::value);
}

TEST_CASE("unique_lock is movable", "[unique_lock]") {
  REQUIRE(kstd::is_move_constructible<kstd::unique_lock<FakeMutex>>::value);
  REQUIRE(kstd::is_move_assignable<kstd::unique_lock<FakeMutex>>::value);
}

TEST_CASE("unique_lock locks on construction", "[unique_lock]") {
  FakeMutex m;
  {
    kstd::unique_lock<FakeMutex> lock(m);
    REQUIRE(m.lock_count == 1);
    REQUIRE(lock.owns_lock());
    REQUIRE(static_cast<bool>(lock));
  }
  REQUIRE(m.unlock_count == 1);
}

TEST_CASE("unique_lock defer_lock does not lock", "[unique_lock]") {
  FakeMutex m;
  kstd::unique_lock<FakeMutex> lock(m, kstd::defer_lock);
  REQUIRE(m.lock_count == 0);
  REQUIRE_FALSE(lock.owns_lock());

  lock.lock();
  REQUIRE(m.lock_count == 1);
  REQUIRE(lock.owns_lock());
}

TEST_CASE("unique_lock try_to_lock", "[unique_lock]") {
  FakeMutex m;
  m.try_lock_result = false;
  kstd::unique_lock<FakeMutex> lock(m, kstd::try_to_lock);
  REQUIRE_FALSE(lock.owns_lock());
}

TEST_CASE("unique_lock move transfers ownership", "[unique_lock]") {
  FakeMutex m;
  kstd::unique_lock<FakeMutex> lock1(m);
  kstd::unique_lock<FakeMutex> lock2(kstd::move(lock1));

  REQUIRE_FALSE(lock1.owns_lock());
  REQUIRE(lock1.mutex() == nullptr);
  REQUIRE(lock2.owns_lock());
  REQUIRE(lock2.mutex() == &m);

  REQUIRE(m.unlock_count == 0);
}

TEST_CASE("unique_lock move assignment unlocks previous", "[unique_lock]") {
  FakeMutex m1;
  FakeMutex m2;
  kstd::unique_lock<FakeMutex> lock1(m1);
  kstd::unique_lock<FakeMutex> lock2(m2);

  lock1 = kstd::move(lock2);
  REQUIRE(m1.unlock_count == 1);
  REQUIRE(lock1.mutex() == &m2);
  REQUIRE_FALSE(lock2.owns_lock());
}

TEST_CASE("unique_lock unlock and manual lock", "[unique_lock]") {
  FakeMutex m;
  kstd::unique_lock<FakeMutex> lock(m);
  lock.unlock();
  REQUIRE(m.unlock_count == 1);
  REQUIRE_FALSE(lock.owns_lock());

  lock.lock();
  REQUIRE(m.lock_count == 2);
  REQUIRE(lock.owns_lock());
}

TEST_CASE("unique_lock release", "[unique_lock]") {
  FakeMutex m;
  kstd::unique_lock<FakeMutex> lock(m);
  auto *released = lock.release();
  REQUIRE(released == &m);
  REQUIRE_FALSE(lock.owns_lock());
  REQUIRE(lock.mutex() == nullptr);
  REQUIRE(m.unlock_count == 0);

  m.unlock();
  REQUIRE(m.unlock_count == 1);
}

TEST_CASE("unique_lock swap", "[unique_lock]") {
  FakeMutex m1;
  FakeMutex m2;
  kstd::unique_lock<FakeMutex> lock1(m1);
  kstd::unique_lock<FakeMutex> lock2(m2);

  kstd::swap(lock1, lock2);
  REQUIRE(lock1.mutex() == &m2);
  REQUIRE(lock2.mutex() == &m1);
}
