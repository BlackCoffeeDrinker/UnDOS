
#include "debug_serial.hpp"
#include "string_view.hpp"
#include "structs.hpp"

namespace kernel::x86::early {

inline void outb(uint16_t port, uint8_t val) {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
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
  // Wait for transmit buffer empty
  while ((inb(COM1 + 5) & 0x20) == 0) {}
  outb(COM1, static_cast<uint8_t>(c));
}
}// namespace kernel::x86::early


namespace kernel::arch {
UNDOS_HAL_API void early_print(const char *str) {
  while (*str) x86::early::write_char(*str++);
}

UNDOS_HAL_API void early_print_char(char c) {
  x86::early::write_char(c);
}

}// namespace kernel::arch
