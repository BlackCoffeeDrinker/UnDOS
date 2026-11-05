#pragma once

#include <kernel/hal_interface.hpp>

namespace test_host_pmm {

void SetUsage(const kernel::PhysicalMemoryUsage &usage) noexcept;
void SetOwnerFrames(uint32_t pid, uint64_t frames) noexcept;
void Reset() noexcept;

}  // namespace test_host_pmm

namespace kernel::task_stats::test_task_stats {
void SetAfterCountHook(void (*hook)() noexcept) noexcept;
}  // namespace test_host_pmm
