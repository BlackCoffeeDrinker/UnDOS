#include "vmm.hpp"
#include "task_stats.hpp"

#include "object_manager.hpp"
#include "scheduler/CpuContext.hpp"
#include "scheduler/Scheduler.hpp"
#include "stdkrn.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KObject.hpp>
#include <kernel/time.hpp>

namespace kernel::task_stats {
namespace {
constexpr size_t MAX_PROCESS_NAME_BYTES = 64;
constexpr uint32_t PIT_TICK_HZ = 100;

void free_staging(
    KObject **objects, TaskProcessRecordV1 *processes, uint8_t *names,
    TaskCpuRecordV1 *cpus) noexcept {
  KE_Free(cpus);
  KE_Free(names);
  KE_Free(processes);
  KE_Free(objects);
}

void release_objects(KObject **objects, size_t count) noexcept {
  for (size_t i = 0; i < count; ++i) KE_OB_Release(objects[i]);
}

bool checked_bytes(size_t count, size_t item_size, size_t &bytes) noexcept {
  if (count && item_size > SIZE_MAX / count) return false;
  bytes = count * item_size;
  return true;
}

void initialize_outputs(__KRN_PROC_GETTASKSNAPSHOT_V1 &args) noexcept {
  args.cpus_len = 0;
  args.processes_len = 0;
  args.names_len = 0;
  args.header = {};
  args.header.version = 1;
  args.requiredCpus = 0;
  args.requiredProcesses = 0;
  args.requiredNameBytes = 0;
  args.writtenCpus = 0;
  args.writtenProcesses = 0;
  args.writtenNameBytes = 0;
}
}// namespace

kstd::error_code capture(__KRN_PROC_GETTASKSNAPSHOT_V1 &args) noexcept {
  initialize_outputs(args);
  if (args.queryOnly > 1) return KernelError::InvalidArgument;
  if (args.queryOnly) {
    if (args.cpus_capacity || args.processes_capacity || args.names_capacity)
      return KernelError::InvalidArgument;
  } else if ((!args.cpus.get() && args.cpus_capacity) ||
             (!args.processes.get() && args.processes_capacity) ||
             (!args.names.get() && args.names_capacity)) {
    return KernelError::InvalidArgument;
  }

  const uintptr_t count_lock = objectmanager::lock_process_directory();
  const size_t process_capacity = objectmanager::process_directory_count_locked();
  objectmanager::unlock_process_directory(count_lock);
  if (process_capacity > UINT32_MAX) return KernelError::ResourceLimitExceeded;

  size_t object_bytes = 0, process_bytes = 0, name_capacity = 0, name_bytes = 0;
  const size_t cpu_capacity = cpu::online_count();
  size_t cpu_bytes = 0;
  if (!checked_bytes(process_capacity, sizeof(KObject *), object_bytes) ||
      !checked_bytes(process_capacity, sizeof(TaskProcessRecordV1), process_bytes) ||
      !checked_bytes(process_capacity, MAX_PROCESS_NAME_BYTES, name_capacity) ||
      !checked_bytes(cpu_capacity, sizeof(TaskCpuRecordV1), cpu_bytes))
    return KernelError::ArithmeticOverflow;

  auto **objects = process_capacity ? static_cast<KObject **>(KE_Malloc(object_bytes)) : nullptr;
  auto *processes = process_capacity
      ? static_cast<TaskProcessRecordV1 *>(KE_Malloc(process_bytes)) : nullptr;
  auto *names = process_capacity ? static_cast<uint8_t *>(KE_Malloc(name_capacity)) : nullptr;
  auto *cpus = cpu_capacity ? static_cast<TaskCpuRecordV1 *>(KE_Malloc(cpu_bytes)) : nullptr;
  if ((process_capacity && (!objects || !processes || !names)) ||
      (cpu_capacity && !cpus)) {
    free_staging(objects, processes, names, cpus);
    return KernelError::OutOfMemory;
  }

  size_t process_count = 0;
  size_t cpu_count = 0;
  bool cpu_valid = false;
  bool memory_valid = true;
  uint64_t owned_bytes = 0;
  PhysicalMemoryUsage usage{};
  TaskSnapshotHeaderV1 header{};
  header.version = 1;

  const uintptr_t process_lock = objectmanager::lock_process_directory();
  if (!objectmanager::capture_processes_locked(objects, process_capacity, process_count) ||
      cpu::online_count() > cpu_capacity) {
    objectmanager::unlock_process_directory(process_lock);
    free_staging(objects, processes, names, cpus);
    return KernelError::Retry;
  }

  if (args.queryOnly) {
    bool names_fit = true;
    for (size_t i = 0; i < process_count; ++i) {
      const auto *process = static_cast<KProcessObject *>(objects[i]);
      const kstd::string_view name(process->name);
      if (name.size() >= MAX_PROCESS_NAME_BYTES ||
          name.size() + 1 > name_capacity - name_bytes ||
          name_bytes > UINT32_MAX) {
        names_fit = false;
        break;
      }
      name_bytes += name.size() + 1;
    }
    args.requiredCpus = cpu::online_count();
    args.requiredProcesses = static_cast<uint32_t>(process_count);
    args.requiredNameBytes = static_cast<uint32_t>(name_bytes);
    objectmanager::unlock_process_directory(process_lock);
    release_objects(objects, process_count);
    free_staging(objects, processes, names, cpus);
    if (!names_fit) return KernelError::ArithmeticOverflow;
    return KernelError::Success;
  }

  auto &contexts = cpu::contexts();
  const uint32_t online_count = cpu::online_count();
  if (online_count != 1) {
    header.cpuReason = TaskCpuUnavailableReason_CpuTopologyUnsupported;
  } else {
    auto &context = contexts[cpu::current_id()];
    header.counterEpoch = context.accounting.counter_epoch;
    header.sampleCycles = context.accounting.total_ticks;
    header.clockHz = context.accounting.clock_available ? PIT_TICK_HZ : 0;
    header.clockResolutionCycles = context.accounting.clock_available ? 1 : 0;
    if (!context.accounting.clock_available) {
      header.cpuReason = TaskCpuUnavailableReason_CpuClockUnavailable;
    } else if (context.accounting.idle_ticks > context.accounting.total_ticks ||
               context.accounting.system_ticks >
                   context.accounting.total_ticks - context.accounting.idle_ticks) {
      header.cpuReason = TaskCpuUnavailableReason_CpuCounterDiscontinuity;
    } else {
      cpu_valid = true;
      header.validity |= TaskMetricValidity_Cpu;
    }
  }

  if (online_count == 1) {
    const auto &context = contexts[cpu::current_id()];
    cpus[cpu_count++] = {
        .cpuId = context.id,
        .validity = cpu_valid ? static_cast<uint32_t>(TaskMetricValidity_Cpu) : 0u,
        .totalCycles = cpu_valid ? context.accounting.total_ticks : 0,
        .idleCycles = cpu_valid ? context.accounting.idle_ticks : 0,
        .systemCycles = cpu_valid ? context.accounting.system_ticks : 0,
    };
  } else {
    for (size_t i = 0; i < contexts.size(); ++i) {
      if (!contexts[i].online) continue;
      cpus[cpu_count++] = {
          .cpuId = contexts[i].id,
          .validity = 0,
          .totalCycles = 0,
          .idleCycles = 0,
          .systemCycles = 0,
      };
    }
  }

  size_t name_offset = 0;
  bool names_valid = true;
  for (size_t i = 0; i < process_count; ++i) {
    auto *process = static_cast<KProcessObject *>(objects[i]);
    const kstd::string_view name(process->name);
    if (name.size() >= MAX_PROCESS_NAME_BYTES ||
        name.size() + 1 > name_capacity - name_offset ||
        name_offset > UINT32_MAX || name.size() + 1 > UINT32_MAX) {
      names_valid = false;
      break;
    }
#if SIZE_MAX > UINT32_MAX
    const uint32_t process_name_bytes =
        static_cast<uint32_t>(name.size() + 1);
#else
    const uint32_t process_name_bytes = name.size() + 1;
#endif
    __builtin_memcpy(names + name_offset, name.data(), name.size());
    names[name_offset + name.size()] = 0;

    uint32_t validity = cpu_valid ? static_cast<uint32_t>(TaskMetricValidity_Cpu) : 0u;
    uint64_t frames = 0;
    const bool owner_valid = HAL_PMM_QueryOwnerFrames(process->pid, &frames);
    uint64_t physical_bytes = 0;
    if (owner_valid && frames <= UINT64_MAX / 4096) {
      physical_bytes = frames * 4096;
      if (owned_bytes > UINT64_MAX - physical_bytes) {
        names_valid = false;
        break;
      }
      owned_bytes += physical_bytes;
      validity |= TaskMetricValidity_Memory;
    } else {
      memory_valid = false;
    }
    if (i == 0) memory_valid = owner_valid;
    else memory_valid = memory_valid && owner_valid;
    processes[i] = {
        .pid = process->pid,
        .incarnation = process->incarnation,
        .state = process->state == KProcessObject::State::Running
            ? static_cast<uint32_t>(TaskProcessState_Running)
            : static_cast<uint32_t>(TaskProcessState_Terminated),
        .validity = validity,
        .cpuCycles = cpu_valid ? process->cpuTicks : 0,
        .physicalBytes = owner_valid ? physical_bytes : 0,
        .nameOffset = static_cast<uint32_t>(name_offset),
        .nameBytes = process_name_bytes,
    };
    name_offset += name.size() + 1;
  }

  const bool pmm_ready = HAL_PMM_QueryUsage(&usage);
  if (!names_valid) {
    objectmanager::unlock_process_directory(process_lock);
    release_objects(objects, process_count);
    free_staging(objects, processes, names, cpus);
    return KernelError::ArithmeticOverflow;
  }

  header.physicalTotalBytes = usage.totalBytes;
  header.physicalUsedBytes = usage.usedBytes;
  header.physicalFreeBytes = usage.freeBytes;
  header.unmanagedPhysicalBytes = usage.unmanagedBytes;
  if (!pmm_ready) {
    header.memoryReason = TaskMemoryUnavailableReason_MemoryManagerUninitialized;
  } else if (!usage.ownershipValid || !memory_valid) {
    header.memoryReason = usage.sharedOwnershipUnsupported
        ? TaskMemoryUnavailableReason_MemorySharedOwnershipUnsupported
        : TaskMemoryUnavailableReason_MemoryOwnershipUnavailable;
    for (size_t i = 0; i < process_count; ++i) {
      processes[i].validity &= ~static_cast<uint32_t>(TaskMetricValidity_Memory);
      processes[i].physicalBytes = 0;
    }
  } else if (owned_bytes > usage.usedBytes) {
    header.memoryReason = TaskMemoryUnavailableReason_MemoryOwnershipUnavailable;
    for (size_t i = 0; i < process_count; ++i) {
      processes[i].validity &= ~static_cast<uint32_t>(TaskMetricValidity_Memory);
      processes[i].physicalBytes = 0;
    }
  } else {
    memory_valid = true;
    header.systemPhysicalBytes = usage.usedBytes - owned_bytes;
    header.validity |= TaskMetricValidity_Memory;
  }
  objectmanager::unlock_process_directory(process_lock);
  args.requiredCpus = static_cast<uint32_t>(cpu_count);
  args.requiredProcesses = static_cast<uint32_t>(process_count);
  if (name_offset > UINT32_MAX) {
    release_objects(objects, process_count);
    free_staging(objects, processes, names, cpus);
    return KernelError::ArithmeticOverflow;
  }
  args.requiredNameBytes = static_cast<uint32_t>(name_offset);
  args.header = header;

  if (args.cpus_capacity < cpu_count ||
      args.processes_capacity < process_count ||
      args.names_capacity < name_offset) {
    release_objects(objects, process_count);
    free_staging(objects, processes, names, cpus);
    return KernelError::InvalidSize;
  }

  if (cpu_count) __builtin_memcpy(args.cpus.get(), cpus, cpu_count * sizeof(*cpus));
  if (process_count)
    __builtin_memcpy(args.processes.get(), processes, process_count * sizeof(*processes));
  if (name_offset) __builtin_memcpy(args.names.get(), names, name_offset);
  args.cpus_len = cpu_count;
  args.processes_len = process_count;
  args.names_len = name_offset;
  args.writtenCpus = static_cast<uint32_t>(cpu_count);
  args.writtenProcesses = static_cast<uint32_t>(process_count);
  args.writtenNameBytes = static_cast<uint32_t>(name_offset);
  release_objects(objects, process_count);
  free_staging(objects, processes, names, cpus);
  return KernelError::Success;
}
}// namespace kernel::task_stats
