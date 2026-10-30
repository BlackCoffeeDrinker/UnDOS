#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string>

#include "../../../Kernel/Drivers/serial_uart/uart_io.hpp"

namespace {
struct FakeUart {
  std::array<uint8_t, 8> registers{};
  std::string output;
  std::string input;
  size_t input_offset = 0;
  size_t fifo_writes = 0;
  uint8_t fifo_value = 0;
  size_t reads = 0;
  size_t writes = 0;
  bool present = true;
  bool transmitter_ready = true;
  bool fail_divisor_write = false;
  bool interrupts_enabled = true;
  size_t irq_saves = 0;
  size_t irq_restores = 0;
};

FakeUart fake;
constexpr uint16_t Base = 0x2F8;

uint8_t ReadPort(void *, uint16_t port) noexcept {
  ++fake.reads;
  const uint16_t reg = port & 7;
  if (reg == 5) {
    if (!fake.present) return 0xFF;
    return static_cast<uint8_t>((fake.input_offset < fake.input.size() ? 0x01 : 0) |
                                (fake.transmitter_ready ? 0x20 : 0));
  }
  if (reg == 0 && (fake.registers[3] & 0x80)) return fake.registers[0];
  if (reg == 1 && (fake.registers[3] & 0x80)) return fake.registers[1];
  if (reg == 0) {
    return fake.input_offset < fake.input.size()
        ? static_cast<uint8_t>(fake.input[fake.input_offset++])
        : 0;
  }
  return fake.registers[reg];
}

void WritePort(void *, uint16_t port, uint8_t value) noexcept {
  ++fake.writes;
  const uint16_t reg = port & 7;
  if (reg == 2) {
    ++fake.fifo_writes;
    fake.fifo_value = value;
    return;
  }
  if (reg == 0 && !(fake.registers[3] & 0x80)) {
    fake.output.push_back(static_cast<char>(value));
    return;
  }
  if (reg == 0 && (fake.registers[3] & 0x80) &&
      fake.fail_divisor_write) {
    return;
  }
  fake.registers[reg] = value;
}

uintptr_t SaveAndDisable(void *) noexcept {
  ++fake.irq_saves;
  const uintptr_t state = fake.interrupts_enabled ? 1 : 0;
  fake.interrupts_enabled = false;
  return state;
}

void RestoreInterrupts(void *, uintptr_t state) noexcept {
  ++fake.irq_restores;
  fake.interrupts_enabled = state != 0;
}

serial_uart::detail::Uart uart(
    {nullptr, ReadPort, WritePort},
    {nullptr, SaveAndDisable, RestoreInterrupts});

struct Fixture {
  Fixture() {
    fake = {};
    fake.registers[3] = 0x03;
    uart = serial_uart::detail::Uart(
        {nullptr, ReadPort, WritePort},
        {nullptr, SaveAndDisable, RestoreInterrupts});
  }
};
}

TEST_CASE_METHOD(Fixture, "COM1 start preserves HAL UART ownership",
                 "[serial][uart]") {
  REQUIRE(uart.Start(0x3F8));
  REQUIRE(fake.reads == 0);
  REQUIRE(fake.writes == 0);
  REQUIRE(fake.fifo_writes == 0);
  REQUIRE(fake.irq_saves == 0);
}

TEST_CASE_METHOD(Fixture, "Other assigned UARTs use compatible 115200 8N1",
                 "[serial][uart]") {
  REQUIRE(uart.Start(Base));
  REQUIRE(fake.registers[0] == 1);
  REQUIRE(fake.registers[1] == 0);
  REQUIRE(fake.registers[3] == 0x03);
  REQUIRE(fake.registers[4] == 0x03);
  REQUIRE(fake.fifo_writes == 1);
  REQUIRE(fake.fifo_value == 0xC7);
  REQUIRE(fake.irq_saves == 1);
  REQUIRE(fake.irq_restores == 1);
  REQUIRE(fake.interrupts_enabled);
  REQUIRE(uart.Start(0x3F8));
  REQUIRE(fake.fifo_writes == 1);
}

TEST_CASE_METHOD(Fixture, "Polled driver transfers are nonblocking and exact",
                 "[serial][uart]") {
  size_t transferred = 99;
  const uint8_t output_bytes[] = {'a', '\r', '\n'};
  REQUIRE_FALSE(uart.Write(0x3F8, output_bytes, sizeof(output_bytes), transferred));
  REQUIRE(transferred == sizeof(output_bytes));
  REQUIRE(fake.output == "a\r\n");
  REQUIRE(fake.irq_saves == 3);
  REQUIRE(fake.irq_restores == 3);

  fake.transmitter_ready = false;
  REQUIRE_FALSE(uart.Write(0x3F8, output_bytes, sizeof(output_bytes), transferred));
  REQUIRE(transferred == 0);
  REQUIRE(fake.output == "a\r\n");

  fake.transmitter_ready = true;
  fake.input = "xy";
  std::array<uint8_t, 4> input{};
  REQUIRE_FALSE(uart.Read(0x3F8, input.data(), input.size(), transferred));
  REQUIRE(transferred == 2);
  REQUIRE(std::string(input.begin(), input.begin() + transferred) == "xy");
}

TEST_CASE_METHOD(Fixture, "Floating ports return unavailable without spinning",
                 "[serial][uart]") {
  size_t transferred = 0;
  const uint8_t byte = 'x';
  fake.present = false;
  REQUIRE(uart.Write(0x3F8, &byte, 1, transferred) ==
          kernel::KernelError::DeviceNotReady);
  REQUIRE(transferred == 0);

  std::array<uint8_t, 1> input{};
  REQUIRE(uart.Read(0x3F8, input.data(), input.size(), transferred) ==
          kernel::KernelError::DeviceNotReady);
  REQUIRE(transferred == 0);
  REQUIRE(fake.irq_saves == 2);
  REQUIRE(fake.irq_restores == 2);
  REQUIRE(fake.interrupts_enabled);
}

TEST_CASE_METHOD(Fixture, "Unavailable assigned UART is rejected at start",
                 "[serial][uart]") {
  fake.present = false;
  REQUIRE_FALSE(uart.Start(Base));
  REQUIRE(fake.writes == 0);
  REQUIRE(fake.interrupts_enabled);
  REQUIRE(fake.irq_saves == fake.irq_restores);
}
