#pragma once

#include <stddef.h>
#include <stdint.h>

#include <undos/syscalls/syscalls.h>

namespace taskmgr {

constexpr uint32_t HistoryLength = 60;
constexpr uint32_t CpuValidity = TaskMetricValidity_Cpu;
constexpr uint32_t MemoryValidity = TaskMetricValidity_Memory;
constexpr uint32_t KnownValidity = CpuValidity | MemoryValidity;

struct Identity {
  uint32_t pid;
  uint64_t incarnation;
};

struct RowRange {
  uint32_t first;
  uint32_t last;
};

inline RowRange visible_row_range(uint32_t first, uint32_t visibleCount,
                                 uint32_t rowCount) noexcept {
  if (first >= rowCount) return {rowCount, rowCount};
  const uint32_t remaining = rowCount - first;
  const uint32_t count = visibleCount < remaining ? visibleCount : remaining;
  return {first, first + count};
}

template <typename Changed>
inline size_t coalesce_row_changes(uint32_t rowCount, Changed changed,
                                   RowRange *ranges,
                                   size_t rangeCapacity) noexcept {
  size_t count = 0;
  for (uint32_t row = 0; row < rowCount; ++row) {
    if (!changed(row)) continue;
    if (count && ranges[count - 1].last == row) {
      ranges[count - 1].last = row + 1;
    } else {
      if (!ranges || count == rangeCapacity) return static_cast<size_t>(-1);
      ranges[count++] = {row, row + 1};
    }
  }
  return count;
}

template <typename SameOrder, typename Changed, typename InvalidateRange,
          typename InvalidateTable>
inline void invalidate_process_update(
    uint32_t previousProcessCount, uint32_t currentProcessCount,
    uint32_t previousScroll, uint32_t currentScroll, uint32_t firstVisible,
    uint32_t visibleCount, SameOrder sameOrder, Changed changed,
    RowRange *ranges, size_t rangeCapacity, InvalidateRange invalidateRange,
    InvalidateTable invalidateTable) {
  if (previousProcessCount != currentProcessCount ||
      currentProcessCount > UINT32_MAX - 2 || previousScroll != currentScroll ||
      !sameOrder()) {
    invalidateTable();
    return;
  }

  const auto visible = visible_row_range(firstVisible, visibleCount,
                                         currentProcessCount + 2);
  const uint32_t rowCount = visible.last - visible.first;
  const size_t rangeCount = coalesce_row_changes(
      rowCount, [&](uint32_t offset) { return changed(visible.first + offset); },
      ranges, rangeCapacity);
  if (rangeCount == static_cast<size_t>(-1)) {
    invalidateTable();
    return;
  }
  for (size_t i = 0; i < rangeCount; ++i)
    invalidateRange({visible.first + ranges[i].first,
                     visible.first + ranges[i].last});
}

struct HistorySlot {
  bool cpuValid;
  uint32_t cpuBasisPoints;
  bool memoryValid;
  uint64_t usedBytes;
  uint64_t totalBytes;
};

struct History {
  HistorySlot slots[HistoryLength]{};
  uint32_t next{0};
  uint32_t count{0};

  void push(const HistorySlot &slot) noexcept {
    slots[next] = slot;
    next = (next + 1) % HistoryLength;
    if (count < HistoryLength) ++count;
  }

  const HistorySlot &at(uint32_t oldestIndex) const noexcept {
    const uint32_t first = (next + HistoryLength - count) % HistoryLength;
    return slots[(first + oldestIndex) % HistoryLength];
  }

  void clear() noexcept {
    for (auto &slot : slots) slot = {};
    next = 0;
    count = 0;
  }
};

inline bool ratio_basis_points(uint64_t numerator, uint64_t denominator,
                               uint32_t &result) noexcept {
  if (!denominator || numerator > denominator) return false;
  if (numerator <= UINT64_MAX / 10000) {
    result = static_cast<uint32_t>(numerator * 10000 / denominator);
    return true;
  }
  uint64_t remainder = 0;
  uint32_t value = 0;
  for (uint32_t i = 0; i < 10000; ++i) {
    if (remainder >= denominator - numerator) {
      remainder -= denominator - numerator;
      ++value;
    } else {
      remainder += numerator;
    }
  }
  result = value;
  return true;
}

inline bool counter_delta(uint64_t current, uint64_t previous,
                          uint64_t &delta) noexcept {
  if (current < previous) return false;
  delta = current - previous;
  return true;
}

inline int find_cpu(const TaskCpuRecordV1 *records, size_t count,
                    uint32_t cpuId) noexcept {
  for (size_t i = 0; i < count; ++i)
    if (records[i].cpuId == cpuId) return static_cast<int>(i);
  return -1;
}

inline int find_process(const TaskProcessRecordV1 *records, size_t count,
                        Identity identity) noexcept {
  for (size_t i = 0; i < count; ++i) {
    if (records[i].pid == identity.pid &&
        records[i].incarnation == identity.incarnation)
      return static_cast<int>(i);
  }
  return -1;
}

inline bool validate_snapshot(const TaskSnapshotHeaderV1 &header,
                              const TaskCpuRecordV1 *cpus, size_t cpuCount,
                              const TaskProcessRecordV1 *processes,
                              size_t processCount, const uint8_t *names,
                              size_t nameBytes) noexcept {
  if (header.version != 1 || (header.validity & ~KnownValidity) ||
      cpuCount > UINT32_MAX || processCount > UINT32_MAX ||
      processCount > UINT32_MAX - 2 ||
      nameBytes > UINT32_MAX || (cpuCount && !cpus) ||
      (processCount && !processes) || (nameBytes && !names))
    return false;

  if (header.cpuReason > TaskCpuUnavailableReason_CpuTopologyUnsupported ||
      header.memoryReason > TaskMemoryUnavailableReason_MemorySharedOwnershipUnsupported)
    return false;
  if (((header.validity & CpuValidity) != 0) !=
      (header.cpuReason == TaskCpuUnavailableReason_CpuAvailable))
    return false;
  if (((header.validity & MemoryValidity) != 0) !=
      (header.memoryReason == TaskMemoryUnavailableReason_MemoryAvailable))
    return false;
  if ((header.validity & CpuValidity) && !cpuCount) return false;

  for (size_t i = 0; i < cpuCount; ++i) {
    const auto &cpu = cpus[i];
    if (cpu.validity & ~CpuValidity) return false;
    if ((header.validity & CpuValidity) != (cpu.validity & CpuValidity))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (cpus[j].cpuId == cpu.cpuId) return false;
    if ((cpu.validity & CpuValidity) &&
        (cpu.idleCycles > cpu.totalCycles || cpu.systemCycles > cpu.totalCycles))
      return false;
  }

  uint64_t ownedBytes = 0;
  for (size_t i = 0; i < processCount; ++i) {
    const auto &process = processes[i];
    if (!process.pid || !process.incarnation ||
        (process.state != TaskProcessState_Running &&
         process.state != TaskProcessState_Terminated) ||
        (process.validity & ~KnownValidity) ||
        process.nameOffset > nameBytes ||
        process.nameBytes > nameBytes - process.nameOffset ||
        !process.nameBytes ||
        names[process.nameOffset + process.nameBytes - 1] != 0)
      return false;
    if ((process.validity & CpuValidity) != (header.validity & CpuValidity) ||
        ((process.validity & MemoryValidity) &&
         !(header.validity & MemoryValidity)))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (processes[j].pid == process.pid &&
          processes[j].incarnation == process.incarnation)
        return false;
    if (process.validity & MemoryValidity) {
      if (ownedBytes > UINT64_MAX - process.physicalBytes) return false;
      ownedBytes += process.physicalBytes;
    } else if (process.physicalBytes) {
      return false;
    }
  }

  if (header.validity & MemoryValidity) {
    if (header.physicalUsedBytes > UINT64_MAX - header.physicalFreeBytes ||
        header.physicalUsedBytes + header.physicalFreeBytes !=
            header.physicalTotalBytes ||
        ownedBytes > header.physicalUsedBytes ||
        header.systemPhysicalBytes != header.physicalUsedBytes - ownedBytes)
      return false;
  }
  return true;
}

inline bool cpu_delta_basis_points(const TaskCpuRecordV1 &current,
                                   const TaskCpuRecordV1 &previous,
                                   uint64_t epoch, uint64_t previousEpoch,
                                   uint32_t &busyBasisPoints,
                                   uint64_t &deltaCycles) noexcept {
  if (epoch != previousEpoch || current.cpuId != previous.cpuId ||
      !(current.validity & CpuValidity) ||
      !(previous.validity & CpuValidity) ||
      !counter_delta(current.totalCycles, previous.totalCycles, deltaCycles))
    return false;
  uint64_t idleDelta = 0;
  if (!counter_delta(current.idleCycles, previous.idleCycles, idleDelta) ||
      idleDelta > deltaCycles)
    return false;
  return ratio_basis_points(deltaCycles - idleDelta, deltaCycles,
                            busyBasisPoints);
}

inline bool process_delta_basis_points(const TaskProcessRecordV1 &current,
                                      const TaskProcessRecordV1 &previous,
                                      uint64_t totalCpuDelta,
                                      uint32_t &basisPoints) noexcept {
  uint64_t processDelta = 0;
  if (current.pid != previous.pid ||
      current.incarnation != previous.incarnation ||
      !(current.validity & CpuValidity) ||
      !(previous.validity & CpuValidity) ||
      !counter_delta(current.cpuCycles, previous.cpuCycles, processDelta))
    return false;
  return ratio_basis_points(processDelta, totalCpuDelta, basisPoints);
}

inline size_t utf8_prefix_bytes(const char *text, size_t bytes,
                                size_t maximumBytes) noexcept {
  size_t end = 0;
  while (end < bytes && end < maximumBytes && text[end]) {
    const uint8_t lead = static_cast<uint8_t>(text[end]);
    size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 :
        (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (width > bytes - end || width > maximumBytes - end) break;
    end += width;
  }
  return end;
}

} // namespace taskmgr
