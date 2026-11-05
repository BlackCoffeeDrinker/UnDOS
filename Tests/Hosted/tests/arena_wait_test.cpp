#include <catch2/catch_test_macros.hpp>
#include "ob_test_support.hpp"
#include "sched_test_hooks.hpp"
#include "memory/ArenaMutex.hpp"
#include "wait.hpp"
#include "scheduler/CpuContext.hpp"
#include "task_manager_scheduler_hal_hooks.hpp"
#include <kernel/hal_interface.hpp>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>

namespace test_smp {
bool InterruptsEnabled() noexcept;
void SetInterruptsEnabled(bool) noexcept;
}

namespace {
struct WaitFixture {
  kernel::KObjectPtr<kernel::KThreadObject> first, second;
  WaitFixture() {
    EnsureObjectManagerInitialized();
    auto a = kernel::CreateKObject<kernel::KThreadObject>("arena-wait-a");
    auto b = kernel::CreateKObject<kernel::KThreadObject>("arena-wait-b");
    REQUIRE(a);
    REQUIRE(b);
    first = *a;
    second = *b;
    first->state = second->state = kernel::KThreadObject::State::Running;
  }
  ~WaitFixture() {
    test_sched::blockHook = nullptr;
    test_sched::beforeWaitHook = nullptr;
    test_sched::SetCurrentThread(nullptr);
    test_smp::SetInterruptsEnabled(true);
  }
};
}

TEST_CASE("R5 arena blocks through production wait and signal with exact IF",
          "[r5][task-manager][wait][responsive-prerequisite]") {
  WaitFixture fixture;
  static kernel::memory::ArenaMutex *lock;
  static bool blocked;
  static kernel::KDispatcherHeader *header;
  for (const bool enabled : {false, true}) {
    kernel::memory::ArenaMutex arena;
    lock = &arena; blocked = false; header = nullptr;
    test_sched::SetCurrentThread(nullptr);
    test_smp::SetInterruptsEnabled(enabled);
    REQUIRE(arena.try_lock()); // Before scheduler/current-thread setup.
    REQUIRE(test_smp::InterruptsEnabled() == enabled);
    test_sched::SetCurrentThread(fixture.first);
    REQUIRE_FALSE(arena.try_lock());
    test_sched::beforeWaitHook = [](kernel::KDispatcherHeader &value) { header = &value; };
    test_sched::blockHook = [] {
      blocked = true;
      REQUIRE_FALSE(test_smp::InterruptsEnabled());
      const auto self = KE_SCHED_GetCurrentThread();
      REQUIRE(self->state == kernel::KThreadObject::State::Blocked);
      REQUIRE(header);
      REQUIRE_FALSE(header->manualReset);
      REQUIRE(header->waiters);
      REQUIRE(header->waiters->thread == self.get());
      REQUIRE(kernel::wait::lock().try_lock());
      kernel::wait::lock().unlock();
      lock->unlock();
      REQUIRE(self->state == kernel::KThreadObject::State::Ready);
      REQUIRE(header->waiters == nullptr);
      REQUIRE_FALSE(header->signalled);
    };
    arena.lock();
    REQUIRE(blocked);
    REQUIRE(fixture.first->state == kernel::KThreadObject::State::Running);
    REQUIRE(test_smp::InterruptsEnabled() == enabled);
    REQUIRE_FALSE(arena.try_lock());
    arena.unlock();
    REQUIRE(test_smp::InterruptsEnabled() == enabled);
  }
}

TEST_CASE("R5 arena consumes a signal before registration without losing release",
          "[r5][task-manager][wait]") {
  WaitFixture fixture;
  kernel::memory::ArenaMutex arena;
  static kernel::memory::ArenaMutex *lock;
  static bool notified, blocked;
  lock = &arena; notified = blocked = false;
  arena.lock();
  test_sched::SetCurrentThread(fixture.first);
  test_sched::beforeWaitHook = [](kernel::KDispatcherHeader &header) {
    REQUIRE(header.waiters == nullptr);
    REQUIRE_FALSE(header.signalled);
    lock->unlock();
    REQUIRE(header.signalled);
    notified = true;
  };
  test_sched::blockHook = [] { blocked = true; };
  arena.lock();
  REQUIRE(notified);
  REQUIRE_FALSE(blocked);
  arena.unlock();
}

TEST_CASE("R5 arena multiple stack waiters receive notifications not ownership",
          "[r5][task-manager][wait]") {
  WaitFixture fixture;
  kernel::memory::ArenaMutex arena;
  static kernel::memory::ArenaMutex *lock;
  static kernel::KThreadObject *first, *second;
  static kernel::KDispatcherHeader *header;
  static unsigned depth;
  lock = &arena; first = fixture.first.get(); second = fixture.second.get();
  depth = 0; header = nullptr;
  arena.lock();
  test_sched::SetCurrentThread(fixture.first);
  test_sched::beforeWaitHook = [](kernel::KDispatcherHeader &value) { header = &value; };
  test_sched::blockHook = [] {
    if (++depth == 1) {
      REQUIRE(first->state == kernel::KThreadObject::State::Blocked);
      test_sched::SetCurrentThread(kernel::KObjectPtr<kernel::KThreadObject>(second));
      lock->lock(); // Registers a second production stack waiter.
      REQUIRE(second->state == kernel::KThreadObject::State::Running);
      REQUIRE_FALSE(lock->try_lock());
      lock->unlock();
      test_sched::SetCurrentThread(kernel::KObjectPtr<kernel::KThreadObject>(first));
    } else {
      REQUIRE(first->state == kernel::KThreadObject::State::Blocked);
      REQUIRE(second->state == kernel::KThreadObject::State::Blocked);
      REQUIRE(header->waiters);
      REQUIRE(header->waiters->next);
      lock->unlock(); // Wake first; it does not yet execute.
      REQUIRE(first->state == kernel::KThreadObject::State::Ready);
      REQUIRE(second->state == kernel::KThreadObject::State::Blocked);
      // Another notification (not a lock transfer) releases second's waiter.
      KE_EVENT_Signal(*header, false);
      REQUIRE(second->state == kernel::KThreadObject::State::Ready);
    }
  };
  arena.lock();
  REQUIRE(depth == 2);
  REQUIRE(first->state == kernel::KThreadObject::State::Running);
  REQUIRE(header->waiters == nullptr);
  REQUIRE_FALSE(arena.try_lock());
  arena.unlock();
}

TEST_CASE("R5 stale arena notification cannot grant ownership while another owner holds the word",
          "[r5][task-manager][wait]") {
  WaitFixture fixture;
  kernel::memory::ArenaMutex arena;
  static kernel::memory::ArenaMutex *lock;
  static unsigned waits, blocks;
  lock = &arena; waits = blocks = 0;
  arena.lock();
  arena.unlock(); // Sticky notification.
  arena.lock();   // An owner barges before that notification is consumed.
  test_sched::SetCurrentThread(fixture.first);
  test_sched::beforeWaitHook = [](kernel::KDispatcherHeader &) { ++waits; };
  test_sched::blockHook = [] {
    ++blocks;
    REQUIRE_FALSE(lock->try_lock());
    lock->unlock();
  };
  arena.lock();
  REQUIRE(waits == 2);
  REQUIRE(blocks == 1);
  REQUIRE_FALSE(arena.try_lock());
  arena.unlock();
}

TEST_CASE("R5 arena IRQ entry and no-current contention fail-stop rather than yield",
          "[r5][task-manager][wait]") {
  WaitFixture fixture;
  test_sched_hal::Reset();
  for (const unsigned scenario : {0u, 1u, 2u}) {
    std::fflush(nullptr);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
      signal(SIGABRT, SIG_DFL);
      const rlimit noCore{0, 0};
      setrlimit(RLIMIT_CORE, &noCore);
      kernel::memory::ArenaMutex arena;
      if (scenario == 1) {
        test_sched::SetCurrentThread(fixture.first);
        test_sched_hal::SetHardwareInterruptDepth(1);
      } else if (scenario == 2) {
        test_sched::SetCurrentThread(nullptr);
        kernel::cpu::current_context().current = nullptr;
        test_sched_hal::SetHardwareInterruptDepth(1);
      } else {
        test_sched::SetCurrentThread(nullptr);
        arena.lock();
      }
      arena.lock();
      _exit(17);
    }
    int status{};
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
  }
}

TEST_CASE("R5 arena bootstrap IRQ nesting is observable before a current thread exists",
          "[r5][task-manager][wait]") {
  WaitFixture fixture;
  test_sched_hal::Reset();
  auto &context = kernel::cpu::current_context();
  REQUIRE_FALSE(context.current);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  test_sched_hal::SetHardwareInterruptDepth(2);
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  test_sched_hal::SetHardwareInterruptDepth(1);
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  test_sched_hal::SetHardwareInterruptDepth(0);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
}
