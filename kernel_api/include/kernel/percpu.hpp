#pragma once

#include <stddef.h>

namespace kernel {

/**
 * @ingroup SYNC
 * @brief Upper bound on the number of logical CPUs this kernel build can
 * track per-CPU state for.
 *
 * There is no HAL that boots more than one logical CPU today, so this is a
 * conservative compile-time cap sized for the SMP-capable HAL(s) this value
 * is expected to serve once they exist.
 */
inline constexpr size_t MAX_CPUS = 32;

namespace cpu {

/**
 * @brief Returns the logical id (0..MAX_CPUS-1) of the CPU executing the
 * caller.
 *
 * Defined in kernel/src/percpu.cpp. Today this always returns 0, since no
 * HAL call exists yet to ask the platform which CPU is running the current
 * code. This is the single place that will be repointed at a real
 * HAL_CPU_GetCurrentId() once an APIC-aware HAL exists; PerCpu<T>'s call
 * sites never need to change.
 */
size_t current_id() noexcept;

}// namespace cpu

/**
 * @ingroup SYNC
 * @brief A fixed-size, per-CPU-slot container.
 *
 * Holds one T per logical CPU (up to N, default MAX_CPUS) and exposes the
 * calling CPU's own slot via current(). Usable and testable today on a
 * single CPU (current_id() always returns 0), and ready to be repointed at
 * a real per-CPU index later without changing any PerCpu<T> call site.
 */
template<typename T, size_t N = MAX_CPUS>
class PerCpu {
 public:
  PerCpu() noexcept = default;

  PerCpu(const PerCpu &) = delete;
  PerCpu &operator=(const PerCpu &) = delete;

  [[nodiscard]] T &current() noexcept { return slots_[cpu::current_id()]; }
  [[nodiscard]] const T &current() const noexcept { return slots_[cpu::current_id()]; }

  [[nodiscard]] T &operator[](size_t idx) noexcept { return slots_[idx]; }
  [[nodiscard]] const T &operator[](size_t idx) const noexcept { return slots_[idx]; }

  [[nodiscard]] static constexpr size_t size() noexcept { return N; }

 private:
  T slots_[N]{};
};

}// namespace kernel
