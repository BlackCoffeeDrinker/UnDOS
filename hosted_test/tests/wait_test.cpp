#include <catch2/catch_test_macros.hpp>

#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KThreadObject.hpp>

// HAL-free wait logic from the kernel source tree.
#include "../../kernel/src/wait.hpp"

using kernel::KDispatcherHeader;
using kernel::KThreadObject;
using kernel::KWaitBlock;
using kernel::wait::TimedWaitList;

namespace {
struct WakeLog {
  KThreadObject *woken[4]{};
  int count{0};
  void operator()(KThreadObject *t) { woken[count++] = t; }
};
} // namespace

TEST_CASE("Dispatcher signal before wait is consumed by auto-reset", "[wait]") {
  KDispatcherHeader h;
  h.manualReset = false;
  WakeLog log;
  REQUIRE(h.Signal(false, log) == 0);
  REQUIRE(h.signalled);
  REQUIRE(h.TryAcquire());
  REQUIRE_FALSE(h.TryAcquire());
}

TEST_CASE("Dispatcher auto-reset wakes one waiter", "[wait]") {
  KDispatcherHeader h;
  h.manualReset = false;
  KThreadObject a, b;
  KWaitBlock wa, wb;
  wa.thread = &a;
  wb.thread = &b;
  h.AddWaiter(wa);
  h.AddWaiter(wb);

  WakeLog log;
  REQUIRE(h.Signal(false, log) == 1);
  REQUIRE(log.woken[0] == &a);
  REQUIRE(wa.satisfied);
  REQUIRE_FALSE(wb.satisfied);
  REQUIRE_FALSE(h.signalled);
  REQUIRE(h.waiters == &wb);
}

TEST_CASE("Dispatcher wake-all and manual-reset", "[wait]") {
  KThreadObject a, b;
  KWaitBlock wa, wb;
  wa.thread = &a;
  wb.thread = &b;

  SECTION("wakeAll on auto-reset releases everyone and stays reset") {
    KDispatcherHeader h;
    h.manualReset = false;
    h.AddWaiter(wa);
    h.AddWaiter(wb);
    WakeLog log;
    REQUIRE(h.Signal(true, log) == 2);
    REQUIRE(h.waiters == nullptr);
    REQUIRE_FALSE(h.signalled);
  }
  SECTION("manual-reset releases everyone and stays signalled") {
    KDispatcherHeader h;
    h.AddWaiter(wa);
    h.AddWaiter(wb);
    WakeLog log;
    REQUIRE(h.Signal(false, log) == 2);
    REQUIRE(log.woken[0] == &a);
    REQUIRE(log.woken[1] == &b);
    REQUIRE(h.signalled);
    REQUIRE(h.TryAcquire());
    REQUIRE(h.TryAcquire());
  }
}

TEST_CASE("Timed waits expire only past their deadline", "[wait]") {
  KDispatcherHeader h;
  KThreadObject a, b;
  KWaitBlock wa, wb;
  wa.thread = &a;
  wa.deadline = kstd::chrono::milliseconds(100);
  wb.thread = &b;
  wb.deadline = kstd::chrono::milliseconds(200);
  h.AddWaiter(wa);
  h.AddWaiter(wb);
  TimedWaitList list;
  list.Add(wa);
  list.Add(wb);

  WakeLog log;
  list.Expire(kstd::chrono::milliseconds(99), log);
  REQUIRE(log.count == 0);
  list.Expire(kstd::chrono::milliseconds(150), log);
  REQUIRE(log.count == 1);
  REQUIRE(log.woken[0] == &a);
  REQUIRE(wa.timed_out);
  REQUIRE_FALSE(wb.timed_out);

  // The waiter removes itself from the header after a timeout.
  REQUIRE(h.RemoveWaiter(wa));
  REQUIRE(h.waiters == &wb);

  // A satisfied block is unlinked without being woken twice.
  WakeLog sig;
  h.Signal(false, sig);
  REQUIRE(wb.satisfied);
  list.Expire(kstd::chrono::milliseconds(500), log);
  REQUIRE(log.count == 1);
  REQUIRE(list.head == nullptr);
}
