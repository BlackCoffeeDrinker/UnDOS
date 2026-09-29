#pragma once

#include <stdint.h>

// Test-only control over the host's stand-in for the CPU-identity and
// interrupt-state HAL surface (implemented in kernel_host_vmm_stubs.cpp).
namespace test_smp {

// Make HAL_CPU_GetCurrentId() report `id` from now on.
void SetCurrentCpuId(uint32_t id) noexcept;

// Back to CPU 0, interrupts enabled.
void Reset() noexcept;

// The host's simulated interrupt-enable flag, as manipulated by
// HAL_CPU_EnableInterrupts / DisableInterrupts / SaveAndDisableInterrupts /
// RestoreInterrupts.
bool InterruptsEnabled() noexcept;

void SetInterruptsEnabled(bool enabled) noexcept;

}// namespace test_smp
