#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {
unsigned int assertion_count;
unsigned int failure_count;
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    ++assertion_count;                                                         \
    if (!(expression)) {                                                       \
      ++failure_count;                                                         \
      std::cerr << "FAIL line " << __LINE__ << ": " #expression "\n";         \
    }                                                                          \
  } while (false)
#define REQUIRE(expression)                                                   \
  do {                                                                         \
    CHECK(expression);                                                         \
    if (!(expression))                                                         \
      return;                                                                  \
  } while (false)

enum class PlatformMemoryOrder : std::uint8_t {
  Relaxed,
  Consume,
  Acquire,
  Release,
  AcquireRelease,
  SequentiallyConsistent
};

void atomic_memory_order(PlatformMemoryOrder) noexcept {}

namespace {
struct HookPlan {
  unsigned int forced_delta = 0;
  unsigned int post_success_delta = 0;
  unsigned int attempts = 0;
  bool injected = false;
  bool post_success_written = false;
};

thread_local HookPlan *active_hook_plan;

void atomic_cmpxchg_test_before_attempt(volatile unsigned int *target) noexcept {
  if (!active_hook_plan)
    return;
  ++active_hook_plan->attempts;
  if (active_hook_plan->forced_delta && !active_hook_plan->injected) {
    const unsigned int delta = active_hook_plan->forced_delta;
    asm volatile("lock addl %k1, %0"
                 : "+m"(*target)
                 : "r"(delta)
                 : "cc", "memory");
    active_hook_plan->injected = true;
  }
}

void atomic_cmpxchg_test_after_attempt(volatile unsigned int *target,
                                      unsigned int returned,
                                      unsigned int expected) noexcept {
  if (!active_hook_plan || !active_hook_plan->post_success_delta ||
      active_hook_plan->post_success_written || returned != expected)
    return;
  const unsigned int delta = active_hook_plan->post_success_delta;
  asm volatile("lock addl %k1, %0"
               : "+m"(*target)
               : "r"(delta)
               : "cc", "memory");
  active_hook_plan->post_success_written = true;
}

#include "../../../Kernel/HAL/x86_old/atomic_cmpxchg_loops.hpp"

struct Operation {
  std::uint64_t started;
  std::uint64_t finished;
  unsigned int old_value;
  unsigned int operand;
  bool subtract;
};

bool history_is_linearizable(const std::vector<Operation> &history,
                             std::uint32_t initial_value) {
  const std::size_t count = history.size();
  if (count > 63)
    return false;
  const std::uint64_t complete = (std::uint64_t{1} << count) - 1;
  std::vector<std::uint64_t> failed_states;
  const auto search = [&](auto &&self, std::uint64_t done,
                          std::uint32_t current) -> bool {
    if (done == complete)
      return true;
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint64_t bit = std::uint64_t{1} << i;
      if (done & bit || history[i].old_value != current)
        continue;
      bool predecessors_done = true;
      for (std::size_t j = 0; j < count; ++j) {
        const std::uint64_t predecessor = std::uint64_t{1} << j;
        if (!(done & predecessor) && j != i &&
            history[j].finished < history[i].started) {
          predecessors_done = false;
          break;
        }
      }
      if (!predecessors_done)
        continue;
      const std::uint32_t next =
          history[i].subtract ? current - history[i].operand
                              : current + history[i].operand;
      if (self(self, done | bit, next))
        return true;
    }
    return false;
  };
  return search(search, 0, initial_value);
}
} // namespace

void test_exact_values_and_wrap() {
  constexpr std::array orders{
      PlatformMemoryOrder::Relaxed, PlatformMemoryOrder::Consume,
      PlatformMemoryOrder::Acquire, PlatformMemoryOrder::Release,
      PlatformMemoryOrder::AcquireRelease,
      PlatformMemoryOrder::SequentiallyConsistent};
  for (const auto order : orders) {
    alignas(4) volatile unsigned int value = 7;
    CHECK(atomic_fetch_add_cmpxchg(const_cast<unsigned int *>(&value), 3,
                                   order) == 7);
    CHECK(value == 10);
    CHECK(atomic_fetch_sub_cmpxchg(const_cast<unsigned int *>(&value), 4,
                                   order) == 10);
    CHECK(value == 6);
    CHECK(atomic_fetch_add_cmpxchg(const_cast<unsigned int *>(&value), 0,
                                   order) == 6);
    CHECK(value == 6);
    CHECK(atomic_fetch_sub_cmpxchg(const_cast<unsigned int *>(&value), 0,
                                   order) == 6);
    CHECK(value == 6);
  }

  alignas(4) volatile unsigned int add_wrap = 0xffffffffu;
  CHECK(atomic_fetch_add_cmpxchg(const_cast<unsigned int *>(&add_wrap), 1,
                                 PlatformMemoryOrder::Relaxed) == 0xffffffffu);
  CHECK(add_wrap == 0);

  alignas(4) volatile unsigned int sub_wrap = 0;
  CHECK(atomic_fetch_sub_cmpxchg(const_cast<unsigned int *>(&sub_wrap), 1,
                                 PlatformMemoryOrder::Relaxed) == 0);
  CHECK(sub_wrap == 0xffffffffu);

  alignas(4) volatile unsigned int repeated = 1;
  CHECK(atomic_fetch_add_cmpxchg(const_cast<unsigned int *>(&repeated), 2,
                                 PlatformMemoryOrder::Relaxed) == 1);
  CHECK(atomic_fetch_add_cmpxchg(const_cast<unsigned int *>(&repeated), 2,
                                 PlatformMemoryOrder::Relaxed) == 3);
  CHECK(atomic_fetch_sub_cmpxchg(const_cast<unsigned int *>(&repeated), 1,
                                 PlatformMemoryOrder::Relaxed) == 5);
  CHECK(repeated == 4);
}

void test_failure_retry_and_post_success_writer() {
  alignas(4) volatile unsigned int add_value = 5;
  HookPlan add_plan{.forced_delta = 10};
  active_hook_plan = &add_plan;
  const auto add_old = atomic_fetch_add_cmpxchg(
      const_cast<unsigned int *>(&add_value), 3,
      PlatformMemoryOrder::Relaxed);
  active_hook_plan = nullptr;
  CHECK(add_old == 15);
  CHECK(add_value == 18);
  CHECK(add_plan.injected);
  CHECK(add_plan.attempts == 2);

  alignas(4) volatile unsigned int sub_value = 20;
  HookPlan sub_plan{.forced_delta = 7};
  active_hook_plan = &sub_plan;
  const auto sub_old = atomic_fetch_sub_cmpxchg(
      const_cast<unsigned int *>(&sub_value), 4,
      PlatformMemoryOrder::Relaxed);
  active_hook_plan = nullptr;
  CHECK(sub_old == 27);
  CHECK(sub_value == 23);
  CHECK(sub_plan.injected);
  CHECK(sub_plan.attempts == 2);

  alignas(4) volatile unsigned int postwrite_value = 5;
  HookPlan postwrite_plan{.post_success_delta = 100};
  active_hook_plan = &postwrite_plan;
  const auto postwrite_old = atomic_fetch_add_cmpxchg(
      const_cast<unsigned int *>(&postwrite_value), 3,
      PlatformMemoryOrder::Relaxed);
  active_hook_plan = nullptr;
  CHECK(postwrite_old == 5);
  CHECK(postwrite_value == 108);
  CHECK(postwrite_plan.post_success_written);
  CHECK(postwrite_plan.attempts == 1);
}

void test_concurrent_add_sub_and_mixed_history() {
  constexpr unsigned int thread_count = 6;
  constexpr unsigned int operations_per_thread = 2000;
  constexpr unsigned int operation_count = thread_count * operations_per_thread;

  alignas(4) volatile unsigned int add_value = 0;
  std::array<std::vector<unsigned int>, thread_count> add_results;
  std::vector<std::thread> workers;
  for (unsigned int thread = 0; thread < thread_count; ++thread) {
    workers.emplace_back([&, thread] {
      auto &results = add_results[thread];
      results.reserve(operations_per_thread);
      for (unsigned int i = 0; i < operations_per_thread; ++i)
        results.push_back(atomic_fetch_add_cmpxchg(
            const_cast<unsigned int *>(&add_value), 1,
            PlatformMemoryOrder::Relaxed));
    });
  }
  for (auto &worker : workers)
    worker.join();
  std::vector<unsigned int> add_history;
  add_history.reserve(operation_count);
  for (auto &results : add_results)
    add_history.insert(add_history.end(), results.begin(), results.end());
  std::sort(add_history.begin(), add_history.end());
  REQUIRE(add_history.size() == operation_count);
  for (unsigned int i = 0; i < operation_count; ++i)
    CHECK(add_history[i] == i);
  CHECK(add_value == operation_count);

  alignas(4) volatile unsigned int sub_value = operation_count;
  std::array<std::vector<unsigned int>, thread_count> sub_results;
  workers.clear();
  for (unsigned int thread = 0; thread < thread_count; ++thread) {
    workers.emplace_back([&, thread] {
      auto &results = sub_results[thread];
      results.reserve(operations_per_thread);
      for (unsigned int i = 0; i < operations_per_thread; ++i)
        results.push_back(atomic_fetch_sub_cmpxchg(
            const_cast<unsigned int *>(&sub_value), 1,
            PlatformMemoryOrder::Relaxed));
    });
  }
  for (auto &worker : workers)
    worker.join();
  std::vector<unsigned int> sub_history;
  sub_history.reserve(operation_count);
  for (auto &results : sub_results)
    sub_history.insert(sub_history.end(), results.begin(), results.end());
  std::sort(sub_history.begin(), sub_history.end());
  REQUIRE(sub_history.size() == operation_count);
  for (unsigned int i = 0; i < operation_count; ++i)
    CHECK(sub_history[i] == i + 1);
  CHECK(sub_value == 0);

  constexpr std::uint32_t mixed_initial = 1000;
  constexpr unsigned int operations_per_worker = 10;
  std::array<Operation, operations_per_worker * 2> history{};
  std::atomic<std::uint64_t> clock{0};
  alignas(4) volatile unsigned int mixed_value = mixed_initial;
  workers.clear();
  for (unsigned int thread = 0; thread < 2; ++thread) {
    workers.emplace_back([&, thread] {
      for (unsigned int i = 0; i < operations_per_worker; ++i) {
        const std::size_t index = thread * operations_per_worker + i;
        const bool subtract = ((thread + i) & 1u) != 0;
        const unsigned int operand = 1 + ((thread * 3 + i) % 5);
        history[index].started = clock.fetch_add(1, std::memory_order_relaxed);
        history[index].operand = operand;
        history[index].subtract = subtract;
        history[index].old_value =
            subtract ? atomic_fetch_sub_cmpxchg(
                           const_cast<unsigned int *>(&mixed_value), operand,
                           PlatformMemoryOrder::Relaxed)
                     : atomic_fetch_add_cmpxchg(
                           const_cast<unsigned int *>(&mixed_value), operand,
                           PlatformMemoryOrder::Relaxed);
        history[index].finished = clock.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  CHECK(history_is_linearizable(
      std::vector<Operation>(history.begin(), history.end()), mixed_initial));
}

int main() {
  test_exact_values_and_wrap();
  test_failure_retry_and_post_success_writer();
  test_concurrent_add_sub_and_mixed_history();
  if (failure_count) {
    std::cerr << failure_count << " of " << assertion_count
              << " assertions failed\n";
    return 1;
  }
  std::cout << "All " << assertion_count << " atomic assertions passed\n";
  return 0;
}
