#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/memory/address.hpp>

namespace test_sched_hal {

void Reset() noexcept;
void SetRoot(kernel::PhysicalAddress root) noexcept;
void SetStack(kernel::VirtualAddress stack) noexcept;
void SetSavedStack(kernel::VirtualAddress stack) noexcept;
void SetHardwareInterruptDepth(uint32_t depth) noexcept;
size_t SwitchCount() noexcept;
kernel::VirtualAddress SwitchFrom(size_t index) noexcept;
kernel::VirtualAddress SwitchTo(size_t index) noexcept;

}  // namespace test_sched_hal
