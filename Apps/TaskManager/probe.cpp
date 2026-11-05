#include <userspace.h>

#include <stdlib.h>

#include "model.hpp"

namespace {

struct Snapshot {
  TaskSnapshotHeaderV1 header{};
  TaskCpuRecordV1 *cpus{nullptr};
  TaskProcessRecordV1 *processes{nullptr};
  uint8_t *names{nullptr};
  uint32_t cpuCapacity{0};
  uint32_t processCapacity{0};
  uint32_t nameCapacity{0};
  uint32_t cpuCount{0};
  uint32_t processCount{0};
  uint32_t nameBytes{0};
};

bool initialize(Snapshot &snapshot) noexcept {
  uint32_t cpuLength = 0, processLength = 0, nameLength = 0;
  uint32_t requiredCpus = 0, requiredProcesses = 0, requiredNames = 0;
  uint32_t writtenCpus = 0, writtenProcesses = 0, writtenNames = 0;
  TaskSnapshotHeaderV1 header{};
  if (UD_SYS_GetTaskSnapshotV1(
          1, nullptr, &cpuLength, nullptr, &processLength, nullptr,
          &nameLength, &header, &requiredCpus, &requiredProcesses,
          &requiredNames, &writtenCpus, &writtenProcesses, &writtenNames) !=
      SyscallReturnCode_OKAY)
    return false;
  snapshot.cpus = requiredCpus
      ? static_cast<TaskCpuRecordV1 *>(
            malloc(static_cast<size_t>(requiredCpus) * sizeof(TaskCpuRecordV1)))
      : nullptr;
  snapshot.processes = requiredProcesses
      ? static_cast<TaskProcessRecordV1 *>(
            malloc(static_cast<size_t>(requiredProcesses) *
                   sizeof(TaskProcessRecordV1)))
      : nullptr;
  snapshot.names = requiredNames
      ? static_cast<uint8_t *>(malloc(requiredNames))
      : nullptr;
  if ((requiredCpus && !snapshot.cpus) ||
      (requiredProcesses && !snapshot.processes) ||
      (requiredNames && !snapshot.names))
    return false;
  snapshot.cpuCapacity = requiredCpus;
  snapshot.processCapacity = requiredProcesses;
  snapshot.nameCapacity = requiredNames;
  return true;
}

bool capture(Snapshot &snapshot) noexcept {
  uint32_t cpuCapacity = snapshot.cpuCapacity;
  uint32_t processCapacity = snapshot.processCapacity;
  uint32_t nameCapacity = snapshot.nameCapacity;
  uint32_t requiredCpus = 0, requiredProcesses = 0, requiredNames = 0;
  uint32_t writtenCpus = 0, writtenProcesses = 0, writtenNames = 0;
  TaskSnapshotHeaderV1 header{};
  const uint32_t status = UD_SYS_GetTaskSnapshotV1(
      0, snapshot.cpus, &cpuCapacity, snapshot.processes, &processCapacity,
      snapshot.names, &nameCapacity, &header, &requiredCpus,
      &requiredProcesses, &requiredNames, &writtenCpus, &writtenProcesses,
      &writtenNames);
  if (status != SyscallReturnCode_OKAY ||
      requiredCpus > snapshot.cpuCapacity ||
      requiredProcesses > snapshot.processCapacity ||
      requiredNames > snapshot.nameCapacity ||
      writtenCpus != requiredCpus ||
      writtenProcesses != requiredProcesses ||
      writtenNames != requiredNames ||
      !taskmgr::validate_snapshot(
          header, snapshot.cpus, writtenCpus, snapshot.processes,
          writtenProcesses, snapshot.names, writtenNames))
    return false;
  snapshot.header = header;
  snapshot.cpuCount = writtenCpus;
  snapshot.processCount = writtenProcesses;
  snapshot.nameBytes = writtenNames;
  return true;
}

int find_self(const Snapshot &snapshot) noexcept {
  constexpr char Prefix[] = "taskprob.elf#";
  for (uint32_t i = 0; i < snapshot.processCount; ++i) {
    const auto &process = snapshot.processes[i];
    if (process.nameBytes <= sizeof(Prefix) - 1 ||
        process.nameOffset > snapshot.nameBytes ||
        process.nameBytes > snapshot.nameBytes - process.nameOffset)
      continue;
    const auto *name = snapshot.names + process.nameOffset;
    size_t j = 0;
    while (j < sizeof(Prefix) - 1 && name[j] == Prefix[j]) ++j;
    if (j == sizeof(Prefix) - 1) return static_cast<int>(i);
  }
  return -1;
}

void append_u64(char *out, size_t capacity, size_t &length,
                uint64_t value) noexcept {
  char reverse[24];
  size_t digits = 0;
  do {
    reverse[digits++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value && digits < sizeof(reverse));
  while (digits && length + 1 < capacity)
    out[length++] = reverse[--digits];
  out[length] = 0;
}

void report(const char *prefix, uint64_t value) noexcept {
  char message[96]{};
  size_t length = 0;
  while (prefix[length] && length + 1 < sizeof(message)) {
    message[length] = prefix[length];
    ++length;
  }
  append_u64(message, sizeof(message), length, value);
  (void)UD_SYS_PrintV1(message);
}

}  // namespace

int main() {
  Snapshot before{};
  if (!initialize(before)) {
    (void)UD_SYS_PrintV1("TaskProbe: FAIL initial snapshot");
    return 1;
  }
  int selfIndex = -1;
  for (uint32_t attempt = 0; attempt < 8; ++attempt) {
    if (!capture(before)) {
      (void)UD_SYS_PrintV1("TaskProbe: FAIL initial snapshot");
      return 1;
    }
    selfIndex = find_self(before);
    if ((before.header.validity & taskmgr::CpuValidity) ||
        before.header.cpuReason !=
            TaskCpuUnavailableReason_CpuCounterDiscontinuity)
      break;
  }
  if (selfIndex < 0 || before.cpuCount != 1 ||
      !(before.header.validity & taskmgr::CpuValidity) ||
      !(before.header.validity & taskmgr::MemoryValidity) ||
      !(before.processes[selfIndex].validity & taskmgr::CpuValidity) ||
      !(before.processes[selfIndex].validity & taskmgr::MemoryValidity)) {
    report("TaskProbe: snapshot validity=", before.header.validity);
    report("TaskProbe: CPU unavailable reason=", before.header.cpuReason);
    report("TaskProbe: memory unavailable reason=", before.header.memoryReason);
    if (before.cpuCount)
      report("TaskProbe: CPU record validity=", before.cpus[0].validity);
    if (selfIndex >= 0)
      report("TaskProbe: process validity=",
             before.processes[selfIndex].validity);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL unavailable process/CPU metrics");
    return 1;
  }
  const uint32_t pid = before.processes[selfIndex].pid;
  const uint64_t initialBytes = before.processes[selfIndex].physicalBytes;
  const uint64_t initialCpu = before.processes[selfIndex].cpuCycles;
  const uint64_t initialRamUsed = before.header.physicalUsedBytes;
  const uint64_t initialRamFree = before.header.physicalFreeBytes;

  uint32_t allocation = 0;
  if (UD_SYS_AllocatePagesV1(
          8192, static_cast<MemoryProtectFlags>(MemoryProtectFlags_Read |
                                                MemoryProtectFlags_Write),
          &allocation) != SyscallReturnCode_OKAY ||
      !allocation) {
    (void)UD_SYS_PrintV1("TaskProbe: FAIL allocate pages");
    return 1;
  }
  if (!capture(before) || find_self(before) < 0 ||
      before.processes[find_self(before)].pid != pid ||
      before.processes[find_self(before)].physicalBytes != initialBytes + 8192) {
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL physical allocation delta");
    return 1;
  }
  report("TaskProbe: allocated bytes observed=",
         before.processes[find_self(before)].physicalBytes - initialBytes);
  report("TaskProbe: process bytes before allocation=", initialBytes);
  report("TaskProbe: process bytes after allocation=",
         before.processes[find_self(before)].physicalBytes);
  report("TaskProbe: RAM used before allocation=", initialRamUsed);
  report("TaskProbe: RAM used after allocation=", before.header.physicalUsedBytes);
  report("TaskProbe: RAM free before allocation=", initialRamFree);
  report("TaskProbe: RAM free after allocation=", before.header.physicalFreeBytes);

  const uint64_t busyProcessStart =
      before.processes[find_self(before)].cpuCycles;
  const uint64_t busyTotalStart = before.header.sampleCycles;
  volatile uint32_t work = 0;
  for (uint32_t i = 0; i < 12000000; ++i) work = work + i;
  (void)work;
  if (!capture(before) || find_self(before) < 0) {
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL busy execution accounting");
    return 1;
  }
  const uint64_t busyProcessEnd =
      before.processes[find_self(before)].cpuCycles;
  const uint64_t busyTotalEnd = before.header.sampleCycles;
  const uint64_t busyProcessDelta = busyProcessEnd >= busyProcessStart
      ? busyProcessEnd - busyProcessStart : UINT64_MAX;
  const uint64_t busyTotalDelta = busyTotalEnd >= busyTotalStart
      ? busyTotalEnd - busyTotalStart : 0;
  report("TaskProbe: busy process cycles=", busyProcessDelta);
  report("TaskProbe: busy total cycles denominator=", busyTotalDelta);
  if (busyProcessEnd <= initialCpu || !busyProcessDelta ||
      !busyTotalDelta || busyProcessDelta > busyTotalDelta) {
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL busy execution accounting");
    return 1;
  }
  (void)UD_SYS_PrintV1("TaskProbe: busy execution observed");

  HANDLE waitEvent = 0;
  if (UD_SYS_CreateEventV1(EventFlags_ManualReset, &waitEvent) !=
      SyscallReturnCode_OKAY) {
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL create wait event");
    return 1;
  }
  if (!capture(before)) {
    (void)UD_SYS_CloseV1(waitEvent);
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL pre-wait snapshot");
    return 1;
  }
  const int beforeWaitIndex = find_self(before);
  const uint64_t beforeWaitCpu = beforeWaitIndex >= 0
      ? before.processes[beforeWaitIndex].cpuCycles : 0;
  const uint64_t beforeWaitTotal = before.header.sampleCycles;
  const uint32_t waitStatus = UD_SYS_WaitForObjectV1(waitEvent, 150);
  const bool waitCaptured = capture(before);
  const int afterWaitIndex = waitCaptured ? find_self(before) : -1;
  const uint64_t processDelta = afterWaitIndex >= 0 &&
      before.processes[afterWaitIndex].cpuCycles >= beforeWaitCpu
      ? before.processes[afterWaitIndex].cpuCycles - beforeWaitCpu : UINT64_MAX;
  const uint64_t totalDelta = before.header.sampleCycles >= beforeWaitTotal
      ? before.header.sampleCycles - beforeWaitTotal : 0;
  report("TaskProbe: blocked process cycles=", processDelta);
  report("TaskProbe: blocked total cycles denominator=", totalDelta);
  (void)UD_SYS_CloseV1(waitEvent);
  if (waitStatus != SyscallReturnCode_TIMEOUT || !waitCaptured ||
      afterWaitIndex < 0 || !totalDelta || processDelta >= totalDelta / 2) {
    (void)UD_SYS_FreePagesV1(allocation, 8192);
    (void)UD_SYS_PrintV1("TaskProbe: FAIL blocked-time accounting");
    return 1;
  }
  (void)UD_SYS_PrintV1("TaskProbe: blocking wait excluded");

  const int beforeFreeIndex = find_self(before);
  const uint64_t processBytesBeforeFree = beforeFreeIndex >= 0
      ? before.processes[beforeFreeIndex].physicalBytes : 0;
  const uint64_t ramUsedBeforeFree = before.header.physicalUsedBytes;
  const uint64_t ramFreeBeforeFree = before.header.physicalFreeBytes;
  if (UD_SYS_FreePagesV1(allocation, 8192) != SyscallReturnCode_OKAY ||
      !capture(before)) {
    (void)UD_SYS_PrintV1("TaskProbe: FAIL free pages");
    return 1;
  }
  const int afterFreeIndex = find_self(before);
  if (afterFreeIndex < 0 ||
      before.processes[afterFreeIndex].physicalBytes != initialBytes) {
    (void)UD_SYS_PrintV1("TaskProbe: FAIL physical free delta");
    return 1;
  }
  report("TaskProbe: process bytes before free=", processBytesBeforeFree);
  report("TaskProbe: process bytes after free=",
         before.processes[afterFreeIndex].physicalBytes);
  report("TaskProbe: RAM used before free=", ramUsedBeforeFree);
  report("TaskProbe: RAM used after free=", before.header.physicalUsedBytes);
  report("TaskProbe: RAM free before free=", ramFreeBeforeFree);
  report("TaskProbe: RAM free after free=", before.header.physicalFreeBytes);
  (void)UD_SYS_PrintV1("TaskProbe: physical free observed");
  free(before.cpus);
  free(before.processes);
  free(before.names);
  uint32_t exitOwnedAllocation = 0;
  if (UD_SYS_AllocatePagesV1(
          4096, static_cast<MemoryProtectFlags>(MemoryProtectFlags_Read |
                                                MemoryProtectFlags_Write),
          &exitOwnedAllocation) != SyscallReturnCode_OKAY ||
      !exitOwnedAllocation) {
    (void)UD_SYS_PrintV1("TaskProbe: FAIL exit-owned allocation");
    return 1;
  }
  report("TaskProbe: exit-owned allocation bytes=", 4096);
  (void)UD_SYS_PrintV1("TaskProbe: PASS real snapshot, busy, block, allocate/free");
  return 0;
}
