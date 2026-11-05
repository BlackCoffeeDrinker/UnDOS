#pragma once

#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/konsole.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/scheduler.hpp>

namespace kernel::memory {
// Private to kernel-arena users; notifications never transfer ownership.
class ArenaMutex {
 public:
  constexpr ArenaMutex() noexcept : notification_{false, false, nullptr} {}

  bool try_lock() noexcept {
    check_context();
    uint32_t expected = 0;
    return HAL_ATOMIC_CompareExchange32(VirtualAddress::from_ptr(&state_),
        expected, 1, PlatformMemoryOrder::Acquire);
  }

  void lock() noexcept {
    if (try_lock()) return;
    if (!KE_SCHED_GetCurrentThread()) fail("Arena contention without a current thread");
    do {
      if (KE_SCHED_WaitForHeader(notification_, kstd::chrono::milliseconds(-1)))
        fail("Arena scheduler wait failed");
    } while (!try_lock());
  }

  void unlock() noexcept {
    HAL_ATOMIC_Store32(VirtualAddress::from_ptr(&state_), 0, PlatformMemoryOrder::Release);
    KE_EVENT_Signal(notification_, false);
  }

 private:
  static void check_context() noexcept {
    if (HAL_CPU_InHardwareInterrupt()) fail("Arena acquisition from hardware IRQ");
  }
  [[noreturn]] static void fail(const char *message) noexcept {
    KE_Print("VMM arena invariant: {}", message);
    HAL_PLATFORM_Panic(message, __FILE__, __LINE__);
  }
  uint32_t state_{};
  KDispatcherHeader notification_;
};
} // namespace kernel::memory
