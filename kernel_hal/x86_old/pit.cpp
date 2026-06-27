
#include "pit.hpp"

#include <kernel/time.hpp>

namespace hal::x86 {

constexpr uint32_t PIT_BASE_FREQUENCY = 1193182;
constexpr uint8_t PIT_COMMAND_PORT = 0x43;
constexpr uint8_t PIT_CHANNEL0_PORT = 0x40;
constexpr uint8_t PIT_CONFIGURE_CMD = 0x36;

void set_system_timer_frequency(uint32_t target_hz) noexcept {
  // 1. Calculate and program the hardware divisor
  uint32_t divisor = PIT_BASE_FREQUENCY / target_hz;
  if (divisor > 0xFFFF) divisor = 0xFFFF;

  asm volatile("outb %0, %1" ::"a"(PIT_CONFIGURE_CMD), "Nd"(PIT_COMMAND_PORT));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(divisor & 0xFF)), "Nd"(PIT_CHANNEL0_PORT));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>((divisor >> 8) & 0xFF)), "Nd"(PIT_CHANNEL0_PORT));

  // 2. Calculate the exact physical time duration of this interval
  // Example: 100Hz -> 10,000,000 ns (10ms)
  // Example: 1000Hz -> 1,000,000 ns (1ms)
  uint32_t nanoseconds_per_tick = 1000000000 / target_hz;

  // 3. Update the Executive kernel's increment scale factor
  KE_Time_SetIncrement(kstd::chrono::nanoseconds(nanoseconds_per_tick));
}
}// namespace kernel::x86::pit
