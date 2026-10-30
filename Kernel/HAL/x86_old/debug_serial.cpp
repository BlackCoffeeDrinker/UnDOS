
#include "debug_uart.hpp"
#include "structs.hpp"

namespace {
uint8_t read_port(void *, uint16_t port) noexcept {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

void write_port(void *, uint16_t port, uint8_t val) noexcept {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uintptr_t save_and_disable(void *) noexcept {
  return HAL_CPU_SaveAndDisableInterrupts();
}

void restore_interrupts(void *, uintptr_t state) noexcept {
  HAL_CPU_RestoreInterrupts(state);
}

constinit hal::x86::detail::ConsoleOutput console_output(
    {nullptr, read_port, write_port},
    {nullptr, save_and_disable, restore_interrupts});
}

kstd::error_code hal::x86::detail::WriteSerialConsole(
    const char *text, size_t size, size_t &transferred) noexcept {
  return console_output.WriteNormal(text, size, transferred);
}

kstd::error_code hal::x86::detail::WriteSerialConsoleEmergency(
    const char *text, size_t size, size_t &transferred) noexcept {
  return console_output.WriteEmergency(text, size, transferred);
}

UNDOS_HAL_API_DEF void HAL_Debug_FirmwarePrint(const char* str, size_t size) noexcept {
  size_t transferred = 0;
  (void)console_output.WriteNormal(str, size, transferred);
}
