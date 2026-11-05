
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
    {.context = nullptr, .read = read_port, .write = write_port},
    {.context = nullptr, .save_and_disable = save_and_disable, .restore = restore_interrupts});
}// namespace

kstd::error_code hal::x86::detail::WriteSerialConsole(
    const char *text, size_t size, size_t &transferred) noexcept {
  return console_output.WriteNormal(text, size, transferred);
}

kstd::error_code hal::x86::detail::WriteSerialConsoleEmergency(
    const char *text, size_t size, size_t &transferred) noexcept {
  return console_output.WriteEmergency(text, size, transferred);
}

UNDOS_HAL_API_DEF void HAL_Debug_FirmwarePrint(const char *str, size_t size) noexcept {
#if 1
  size_t transferred = 0;
  (void) console_output.WriteNormal(str, size, transferred);
#else
  // Temporary VGA text console (80x25 at 0xB8000, identity-mapped at entry).
  constexpr size_t kCols = 80, kRows = 25;
  constexpr uint16_t kAttr = 0x0700;
  static size_t col = 0, row = 0;
  auto *vga = reinterpret_cast<volatile uint16_t *>(0xB8000);
  const uintptr_t flags = HAL_CPU_SaveAndDisableInterrupts();
  for (size_t i = 0; i < size; ++i) {
    const char ch = str[i];
    if (ch == '\r') {
      col = 0;
      continue;
    }
    if (ch == '\n') {
      col = 0;
      ++row;
    } else {
      vga[row * kCols + col] = static_cast<uint16_t>(kAttr | static_cast<uint8_t>(ch));
      if (++col == kCols) {
        col = 0;
        ++row;
      }
    }
    if (row == kRows) {
      for (size_t j = 0; j < (kRows - 1) * kCols; ++j) vga[j] = vga[j + kCols];
      for (size_t j = 0; j < kCols; ++j) vga[(kRows - 1) * kCols + j] = kAttr | ' ';
      row = kRows - 1;
    }
  }
  HAL_CPU_RestoreInterrupts(flags);
#endif
}
