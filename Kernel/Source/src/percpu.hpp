#pragma once

#include <stddef.h>

namespace kernel {

/**
 * @ingroup SYNC
 * @brief Upper bound on the number of logical CPUs this kernel build can
 * track per-CPU state for.
 *
 * A conservative compile-time cap. A HAL reporting an id at or beyond this
 * bound is clamped by cpu::current_id() rather than indexing out of range.
 */
inline constexpr size_t MAX_CPUS = 32;

namespace cpu {

/**
 * @brief Returns the logical id (0..MAX_CPUS-1) of the CPU executing the
 * caller.
 *
 * Defined in kernel/src/percpu.cpp as HAL_CPU_GetCurrentId(), clamped to
 * MAX_CPUS. A uniprocessor HAL reports 0, so PerCpu<T> collapses to a
 * single slot there without any call site changing.
 */
size_t current_id() noexcept;

}// namespace cpu

/**
 * @ingroup SYNC
 * @brief A fixed-size, per-CPU-slot container.
 *
 * Holds one T per logical CPU (up to N, default MAX_CPUS) and exposes the
 * calling CPU's own slot via current(), which is indexed by the CPU id the
 * HAL reports. Slots other than the caller's are reachable through
 * operator[]; guarding cross-CPU access is the caller's business.
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
