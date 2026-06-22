
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

void write_char(char c) {
  if (!debug_serial_initialized) {
    debug_serial_initialized = true;
    init_serial();
  }

  // Wait for transmit buffer empty
  while ((inb(COM1 + 5) & 0x20) == 0) {}
  outb(COM1, static_cast<uint8_t>(c));
}
}// namespace

UNDOS_HAL_API void early_print(const char *str) {
  while (*str) write_char(*str++);
}

UNDOS_HAL_API void early_print_char(char c) {
  write_char(c);
}
