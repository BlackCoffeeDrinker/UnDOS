#include <catch2/catch_test_macros.hpp>

#include "../../../Apps/TaskManager/model.hpp"
#include "../../../Kernel/HAL/x86_old/pmm_accounting.hpp"
#include "../../../Kernel/Source/src/scheduler/ExecutionAccounting.hpp"

TEST_CASE("Task Manager checked utilization arithmetic",
          "[task-manager]") {
  uint32_t basisPoints = 77;
  REQUIRE(taskmgr::ratio_basis_points(1, 3, basisPoints));
  REQUIRE(basisPoints == 3333);
  REQUIRE(taskmgr::ratio_basis_points(5, 5, basisPoints));
  REQUIRE(basisPoints == 10000);

  basisPoints = 77;
  REQUIRE_FALSE(taskmgr::ratio_basis_points(1, 0, basisPoints));
  REQUIRE(basisPoints == 77);
  REQUIRE_FALSE(taskmgr::ratio_basis_points(6, 5, basisPoints));
  REQUIRE(basisPoints == 77);

  uint64_t delta = 0;
  REQUIRE(taskmgr::counter_delta(100, 25, delta));
  REQUIRE(delta == 75);
  REQUIRE_FALSE(taskmgr::counter_delta(25, 100, delta));
  REQUIRE(delta == 75);
}

TEST_CASE("Task Manager deltas require the same epoch and incarnation",
          "[task-manager]") {
  const TaskCpuRecordV1 previousCpu{
      .cpuId = 0,
      .validity = TaskMetricValidity_Cpu,
      .totalCycles = 800,
      .idleCycles = 100,
  };
  const TaskCpuRecordV1 currentCpu{
      .cpuId = 0,
      .validity = TaskMetricValidity_Cpu,
      .totalCycles = 1000,
      .idleCycles = 200,
  };
  uint32_t busy = 0;
  uint64_t delta = 0;
  REQUIRE(taskmgr::cpu_delta_basis_points(
      currentCpu, previousCpu, 4, 4, busy, delta));
  REQUIRE(busy == 5000);
  REQUIRE(delta == 200);
  REQUIRE_FALSE(taskmgr::cpu_delta_basis_points(
      currentCpu, previousCpu, 5, 4, busy, delta));

  const TaskProcessRecordV1 previousProcess{
      .pid = 8,
      .incarnation = 22,
      .validity = TaskMetricValidity_Cpu,
      .cpuCycles = 40,
  };
  const TaskProcessRecordV1 currentProcess{
      .pid = 8,
      .incarnation = 22,
      .validity = TaskMetricValidity_Cpu,
      .cpuCycles = 50,
  };
  uint32_t process = 0;
  REQUIRE(taskmgr::process_delta_basis_points(
      currentProcess, previousProcess, delta, process));
  REQUIRE(process == 500);
  auto reusedProcess = currentProcess;
  reusedProcess.incarnation = 23;
  REQUIRE_FALSE(taskmgr::process_delta_basis_points(
      reusedProcess, previousProcess, delta, process));
}

TEST_CASE("Task Manager validates complete snapshot partitions",
          "[task-manager]") {
  const uint8_t names[] = {'a', 'p', 'p', 0};
  TaskCpuRecordV1 cpu{
      .cpuId = 0,
      .validity = TaskMetricValidity_Cpu,
      .totalCycles = 100,
      .idleCycles = 20,
      .systemCycles = 10,
  };
  TaskProcessRecordV1 process{
      .pid = 1,
      .incarnation = 9,
      .state = TaskProcessState_Running,
      .validity = TaskMetricValidity_Cpu | TaskMetricValidity_Memory,
      .cpuCycles = 30,
      .physicalBytes = 4096,
      .nameOffset = 0,
      .nameBytes = sizeof(names),
  };
  TaskSnapshotHeaderV1 header{
      .version = 1,
      .validity = TaskMetricValidity_Cpu | TaskMetricValidity_Memory,
      .cpuReason = TaskCpuUnavailableReason_CpuAvailable,
      .memoryReason = TaskMemoryUnavailableReason_MemoryAvailable,
      .physicalTotalBytes = 8192,
      .physicalUsedBytes = 4096,
      .physicalFreeBytes = 4096,
      .systemPhysicalBytes = 0,
  };
  REQUIRE(taskmgr::validate_snapshot(header, &cpu, 1, &process, 1,
                                     names, sizeof(names)));

  SECTION("rejects inconsistent RAM accounting") {
    header.systemPhysicalBytes = 4096;
    REQUIRE_FALSE(taskmgr::validate_snapshot(header, &cpu, 1, &process, 1,
                                             names, sizeof(names)));
  }

  SECTION("rejects malformed bounded process names") {
    const uint8_t unterminated[] = {'a', 'p', 'p', 'x'};
    REQUIRE_FALSE(taskmgr::validate_snapshot(
        header, &cpu, 1, &process, 1, unterminated, sizeof(unterminated)));
  }

  SECTION("rejects a repeated CPU identity") {
    TaskCpuRecordV1 cpus[] = {cpu, cpu};
    REQUIRE_FALSE(taskmgr::validate_snapshot(header, cpus, 2, &process, 1,
                                             names, sizeof(names)));
  }
}

TEST_CASE("Task Manager preserves incarnation and bounded history",
          "[task-manager]") {
  TaskProcessRecordV1 processes[] = {
      {.pid = 7, .incarnation = 10},
      {.pid = 7, .incarnation = 11},
  };
  REQUIRE(taskmgr::find_process(processes, 2, {7, 10}) == 0);
  REQUIRE(taskmgr::find_process(processes, 2, {7, 11}) == 1);
  REQUIRE(taskmgr::find_process(processes, 2, {7, 12}) == -1);

  taskmgr::History history{};
  for (uint32_t i = 0; i <= taskmgr::HistoryLength; ++i) {
    taskmgr::HistorySlot slot{};
    slot.memoryValid = true;
    slot.usedBytes = i;
    history.push(slot);
  }
  REQUIRE(history.count == taskmgr::HistoryLength);
  REQUIRE(history.at(0).usedBytes == 1);
  REQUIRE(history.at(taskmgr::HistoryLength - 1).usedBytes ==
          taskmgr::HistoryLength);
  history.push({});
  REQUIRE_FALSE(history.at(taskmgr::HistoryLength - 1).memoryValid);
}

TEST_CASE("Task Manager UTF-8 clipping keeps complete code points",
          "[task-manager]") {
  constexpr char text[] = "A\xc2\xa2\xe2\x82\xac";
  REQUIRE(taskmgr::utf8_prefix_bytes(text, sizeof(text) - 1, 4) == 3);
  REQUIRE(taskmgr::utf8_prefix_bytes(text, sizeof(text) - 1, 6) == 6);
}

TEST_CASE("PMM owner ledger counts same-owner aliases once and rejects cross-owner aliases",
          "[task-manager][pmm]") {
  hal::x86::FrameOwnerLedger<8, 3> ledger;
  ledger.reset();
  REQUIRE(ledger.register_owner(7));
  REQUIRE(ledger.register_owner(8));
  REQUIRE_FALSE(ledger.register_owner(7));
  REQUIRE_FALSE(ledger.register_owner(9));

  REQUIRE(ledger.assign(7, 3, true, true) ==
          hal::x86::FrameOwnerAssignment::Assigned);
  REQUIRE(ledger.assign(7, 3, true, true) ==
          hal::x86::FrameOwnerAssignment::AlreadyOwned);
  REQUIRE(ledger.assign(8, 3, true, true) ==
          hal::x86::FrameOwnerAssignment::Shared);
  uint64_t frames = 0;
  REQUIRE(ledger.query(7, frames));
  REQUIRE(frames == 1);
  REQUIRE(ledger.query(8, frames));
  REQUIRE(frames == 0);

  REQUIRE(ledger.assign(7, 1, false, true) ==
          hal::x86::FrameOwnerAssignment::Invalid);
  REQUIRE(ledger.assign(7, 1, true, false) ==
          hal::x86::FrameOwnerAssignment::Invalid);
  REQUIRE_FALSE(ledger.unregister_owner(7));
  REQUIRE(ledger.release(3));
  REQUIRE(ledger.query(7, frames));
  REQUIRE(frames == 0);
  REQUIRE(ledger.unregister_owner(7));
  REQUIRE_FALSE(ledger.query(7, frames));
}

TEST_CASE("PMM allocation eligibility and owned multi-frame free are atomic",
          "[task-manager][pmm]") {
  uint32_t eligible[1]{};
  uint32_t allocated[1]{};
  uint32_t permanent[1]{};
  const auto mark = [](uint32_t *bitmap, size_t frame) {
    bitmap[0] |= 1u << frame;
  };
  mark(eligible, 1);
  mark(eligible, 2);
  mark(eligible, 3);
  mark(allocated, 1);
  mark(allocated, 2);
  mark(allocated, 3);
  mark(permanent, 3);
  REQUIRE(hal::x86::frame_available(0, eligible, allocated, permanent) == false);
  REQUIRE(hal::x86::frame_available(1, eligible, allocated, permanent) == false);

  hal::x86::FrameOwnerLedger<8, 3> ledger;
  ledger.reset();
  REQUIRE(ledger.register_owner(11));
  REQUIRE(ledger.assign(11, 1, true, true) ==
          hal::x86::FrameOwnerAssignment::Assigned);
  REQUIRE(ledger.assign(11, 2, true, true) ==
          hal::x86::FrameOwnerAssignment::Assigned);
  REQUIRE(ledger.assign(11, 3, true, true) ==
          hal::x86::FrameOwnerAssignment::Assigned);

  size_t freed = 0;
  const auto release = [&](size_t frame) {
    REQUIRE(ledger.release(frame));
    allocated[0] &= ~(1u << frame);
    ++freed;
  };
  REQUIRE_FALSE(ledger.release_region(
      11, 4096, 3, 4096, 8, eligible, allocated, permanent, release));
  REQUIRE(freed == 0);
  REQUIRE(ledger.owner(1) == 11);
  REQUIRE(ledger.owner(2) == 11);
  REQUIRE(ledger.owner(3) == 11);

  mark(permanent, 3);
  permanent[0] &= ~(1u << 3);
  REQUIRE_FALSE(ledger.release_region(
      11, 4097, 1, 4096, 8, eligible, allocated, permanent, release));
  REQUIRE_FALSE(ledger.release_region(
      12, 4096, 2, 4096, 8, eligible, allocated, permanent, release));
  REQUIRE_FALSE(ledger.release_region(
      11, 7 * 4096, 2, 4096, 8, eligible, allocated, permanent, release));
  REQUIRE_FALSE(ledger.release_region(
      11, 4096, 2, 4096, 8, eligible, allocated, nullptr, release));
  REQUIRE(ledger.release_region(
      11, 4096, 2, 4096, 8, eligible, allocated, permanent, release));
  REQUIRE(freed == 2);
  uint64_t frames = 0;
  REQUIRE(ledger.query(11, frames));
  REQUIRE(frames == 1);
}

TEST_CASE("Address-space teardown releases only owned user mappings",
          "[task-manager][pmm][vmm]") {
  uint32_t releasedOwner = 0;
  uintptr_t releasedFrame = 0;
  const auto releaser = [&](uint32_t owner, uintptr_t frame) {
    releasedOwner = owner;
    releasedFrame = frame;
    return true;
  };

  REQUIRE(hal::x86::reclaim_owned_mapping(9, true, true, 0x12345000,
                                          releaser));
  REQUIRE(releasedOwner == 9);
  REQUIRE(releasedFrame == 0x12345000);
  releasedOwner = 0;
  REQUIRE(hal::x86::reclaim_owned_mapping(9, true, false, 0x2000,
                                          releaser));
  REQUIRE(hal::x86::reclaim_owned_mapping(9, false, true, 0x3000,
                                          releaser));
  REQUIRE(hal::x86::reclaim_owned_mapping(0, true, true, 0x4000,
                                          releaser));
  REQUIRE(releasedOwner == 0);
}

TEST_CASE("Timer ticks partition interrupted process, system and exact idle",
          "[task-manager][scheduler]") {
  kernel::sched::ExecutionAccounting state{};
  state.clock_available = true;
  uint64_t process = 0, user = 0, kernel = 0;
  for (auto mode : {kernel::InterruptedMode::User, kernel::InterruptedMode::Kernel}) {
    const auto classification = kernel::sched::classify_execution(true, true, false);
    kernel::sched::account_timer_tick(state, classification, mode, &process, &user, &kernel);
  }
  REQUIRE(process == 2);
  REQUIRE(user == 1);
  REQUIRE(kernel == 1);
  for (bool hasThread : {false, true}) {
    const auto classification = kernel::sched::classify_execution(hasThread, false, false);
    kernel::sched::account_timer_tick(state, classification, kernel::InterruptedMode::Kernel, nullptr, nullptr, nullptr);
  }
  kernel::sched::account_timer_tick(
      state, kernel::sched::classify_execution(true, true, true),
      kernel::InterruptedMode::Kernel,
      &process, &user, &kernel);
  REQUIRE(process == 2);
  REQUIRE(state.system_ticks == 2);
  REQUIRE(state.idle_ticks == 1);
  REQUIRE(state.total_ticks == state.system_ticks + state.idle_ticks + process);
  REQUIRE(state.counter_epoch == 1);
}

TEST_CASE("Tick counter overflow resets its baseline once and never wraps the epoch",
          "[task-manager][scheduler]") {
  kernel::sched::ExecutionAccounting state{};
  state.clock_available = true;
  uint64_t process = UINT64_MAX, user = UINT64_MAX, kernel = 0;
  kernel::sched::account_timer_tick(state, kernel::sched::ExecutionClass::Process,
                                    kernel::InterruptedMode::User, &process, &user, &kernel);
  REQUIRE(state.counter_epoch == 2);
  REQUIRE(process == 1);
  REQUIRE(user == 1);
  REQUIRE(kernel == 0);
  REQUIRE(state.total_ticks == 1);
  state.total_ticks = UINT64_MAX;
  state.idle_ticks = 5;
  kernel::sched::account_timer_tick(state, kernel::sched::ExecutionClass::Idle,
                                    kernel::InterruptedMode::Kernel, nullptr, nullptr, nullptr);
  REQUIRE(state.counter_epoch == 3);
  REQUIRE(state.idle_ticks == 1);
  REQUIRE(state.total_ticks == 1);
  state.counter_epoch = UINT64_MAX;
  state.total_ticks = UINT64_MAX;
  kernel::sched::account_timer_tick(state, kernel::sched::ExecutionClass::System,
                                    kernel::InterruptedMode::Kernel, nullptr, nullptr, nullptr);
  REQUIRE_FALSE(state.clock_available);
  REQUIRE(state.counter_epoch == UINT64_MAX);
  REQUIRE(state.total_ticks == UINT64_MAX);
}

TEST_CASE("Task Manager coalesces only adjacent changed display rows",
          "[task-manager][invalidation]") {
  const uint8_t changed[] = {0, 1, 1, 0, 1, 0, 1, 1};
  taskmgr::RowRange ranges[8]{};
  const size_t count = taskmgr::coalesce_row_changes(
      8, [&](uint32_t row) { return changed[row] != 0; }, ranges, 8);
  REQUIRE(count == 3);
  REQUIRE(ranges[0].first == 1);
  REQUIRE(ranges[0].last == 3);
  REQUIRE(ranges[1].first == 4);
  REQUIRE(ranges[1].last == 5);
  REQUIRE(ranges[2].first == 6);
  REQUIRE(ranges[2].last == 8);

  const size_t unchanged = taskmgr::coalesce_row_changes(
      8, [](uint32_t) { return false; }, ranges, 8);
  REQUIRE(unchanged == 0);
  REQUIRE(taskmgr::coalesce_row_changes(
              8, [](uint32_t row) { return row == 1 || row == 3; },
              ranges, 1) == static_cast<size_t>(-1));

  const auto clipped = taskmgr::visible_row_range(UINT32_MAX - 2, 10,
                                                   UINT32_MAX);
  REQUIRE(clipped.first == UINT32_MAX - 2);
  REQUIRE(clipped.last == UINT32_MAX);
  const auto beyond = taskmgr::visible_row_range(UINT32_MAX, 10,
                                                  UINT32_MAX);
  REQUIRE(beyond.first == UINT32_MAX);
  REQUIRE(beyond.last == UINT32_MAX);
}

TEST_CASE("Task Manager sample invalidation emits rows and clears stale tails",
          "[task-manager][invalidation]") {
  struct DisplayRow {
    uint32_t identity;
    uint32_t renderedValue;
  };
  const DisplayRow previous[] = {{10, 1}, {20, 2}, {30, 3}, {0, 4}, {0, 5}};
  DisplayRow current[] = {{10, 1}, {20, 2}, {30, 3}, {0, 4}, {0, 5}};
  taskmgr::RowRange scratch[8]{};
  taskmgr::RowRange requested[8]{};
  size_t requestedCount = 0;
  bool tableRequested = false;

  auto run = [&](uint32_t oldCount, uint32_t newCount,
                 auto sameOrder, auto changed) {
    requestedCount = 0;
    tableRequested = false;
    taskmgr::invalidate_process_update(
        oldCount, newCount, 0, 0, 0, 8, sameOrder, changed, scratch, 8,
        [&](taskmgr::RowRange range) {
          requested[requestedCount++] = range;
        }, [&] { tableRequested = true; });
  };
  auto sameOrder = [&] {
    for (uint32_t i = 0; i < 3; ++i)
      if (previous[i].identity != current[i].identity) return false;
    return true;
  };
  auto changed = [&](uint32_t row) {
    return previous[row].identity != current[row].identity ||
           previous[row].renderedValue != current[row].renderedValue;
  };

  run(3, 3, sameOrder, changed);
  REQUIRE_FALSE(tableRequested);
  REQUIRE(requestedCount == 0);

  current[1].renderedValue = 9;
  current[2].renderedValue = 10;
  run(3, 3, sameOrder, changed);
  REQUIRE_FALSE(tableRequested);
  REQUIRE(requestedCount == 1);
  REQUIRE(requested[0].first == 1);
  REQUIRE(requested[0].last == 3);

  run(3, 2, sameOrder, changed);
  REQUIRE(tableRequested);
  REQUIRE(requestedCount == 0);

  current[0].identity = 30;
  current[1].identity = 10;
  current[2].identity = 20;
  run(3, 3, sameOrder, changed);
  REQUIRE(tableRequested);
  REQUIRE(requestedCount == 0);
}
