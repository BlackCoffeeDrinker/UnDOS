
#include "string_view.hpp"
#include "structs.hpp"

namespace {
enum Port : uint16_t { COM1 = 0x3F8 };
bool debug_serial_initialized = false;

void outb(uint16_t port, uint8_t val) {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

void init_serial() {
  outb(COM1 + 1, 0x00);// Disable all interrupts
  outb(COM1 + 3, 0x80);// Enable DLAB (set baud rate divisor)
  outb(COM1 + 0, 0x03);// Set divisor to 3 (38400 baud)
  outb(COM1 + 1, 0x00);// (hi byte)
  outb(COM1 + 3, 0x03);// 8 bits, no parity, one stop bit
  outb(COM1 + 2, 0xC7);// Enable FIFO, clear them, with 14-byte threshold
}

}// namespace

UNDOS_HAL_API_DEF void HAL_Debug_FirmwarePrint(const char* str, size_t size) noexcept {
  if (!str || size == 0) {
    return;
  }

  // Ensure serial port is initialized once per boot
  if (!debug_serial_initialized) {
    debug_serial_initialized = true;
    init_serial();
  }

  // Write each character in the buffer to UART COM1
  for (size_t i = 0; i < size; ++i) {
    // Wait for Transmit Holding Register Empty (Line Status Register bit 5)
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, static_cast<uint8_t>(str[i]));
  }
}
