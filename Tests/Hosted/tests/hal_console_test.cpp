#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string>

#include "../../../Kernel/HAL/x86_old/debug_uart.hpp"
#include "../../../Kernel/HAL/x86_old/console.cpp"

namespace {
struct FakeUart {
  std::array<uint8_t, 8> registers{};
  std::string output;
  bool present = true;
  bool fail_divisor_write = false;
  size_t fifo_writes = 0;
  uint8_t fifo_value = 0;
  size_t lsr_reads = 0;
  size_t byte_polls = 0;
  size_t configuration_lsr_reads = 2;
  size_t polls_before_ready = 0;
  size_t irq_saves = 0;
  size_t irq_restores = 0;
  bool interrupts_enabled = true;
};

FakeUart fake;

uint8_t ReadPort(void *, uint16_t port) noexcept {
  const uint16_t reg = port - 0x3F8;
  if (reg == 5) {
    ++fake.lsr_reads;
    if (!fake.present) return 0xFF;
    if (fake.configuration_lsr_reads) {
      --fake.configuration_lsr_reads;
      return 0x60;
    }
    if (fake.byte_polls++ >= fake.polls_before_ready) return 0x60;
    return 0;
  }
  if (reg == 0 && (fake.registers[3] & 0x80)) {
    return fake.registers[0];
  }
  if (reg == 1 && (fake.registers[3] & 0x80)) {
    return fake.registers[1];
  }
  return fake.registers[reg];
}

void WritePort(void *, uint16_t port, uint8_t value) noexcept {
  const uint16_t reg = port - 0x3F8;
  if (reg == 0 && !(fake.registers[3] & 0x80)) {
    fake.output.push_back(static_cast<char>(value));
    fake.byte_polls = 0;
    return;
  }
  if (reg == 2) {
    ++fake.fifo_writes;
    fake.fifo_value = value;
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

hal::x86::detail::ConsoleOutput output(
    {nullptr, ReadPort, WritePort},
    {nullptr, SaveAndDisable, RestoreInterrupts});

struct Fixture {
  Fixture() {
    fake = {};
    fake.registers[3] = 0x03;
    output = hal::x86::detail::ConsoleOutput(
        {nullptr, ReadPort, WritePort},
        {nullptr, SaveAndDisable, RestoreInterrupts});
  }
};

}

kstd::error_code hal::x86::detail::WriteSerialConsole(
    const char *text, size_t size, size_t &transferred) noexcept {
  return output.WriteNormal(text, size, transferred);
}

kstd::error_code hal::x86::detail::WriteSerialConsoleEmergency(
    const char *text, size_t size, size_t &transferred) noexcept {
  return output.WriteEmergency(text, size, transferred);
}

uintptr_t HAL_CPU_SaveAndDisableInterrupts() noexcept {
  return SaveAndDisable(nullptr);
}

void HAL_CPU_RestoreInterrupts(uintptr_t state) noexcept {
  RestoreInterrupts(nullptr, state);
}

TEST_CASE_METHOD(Fixture, "HAL console writes exact bytes through the bounded UART",
                 "[console][hal]") {
  size_t transferred = 99;
  REQUIRE_FALSE(HAL_CONSOLE_Write("A\r\n", 3, transferred));
  REQUIRE(transferred == 3);
  REQUIRE(fake.output == "A\r\n");
  REQUIRE(fake.registers[0] == 1);
  REQUIRE(fake.registers[1] == 0);
  REQUIRE(fake.registers[3] == 0x03);
  REQUIRE(fake.registers[4] == 0x03);
  REQUIRE(fake.fifo_writes == 1);
  REQUIRE(fake.fifo_value == 0xC7);
  REQUIRE(fake.irq_saves == 1);
  REQUIRE(fake.irq_restores == 1);
  REQUIRE(fake.interrupts_enabled);

  REQUIRE_FALSE(HAL_CONSOLE_Write("B", 1, transferred));
  REQUIRE(fake.fifo_writes == 1);
}

TEST_CASE_METHOD(Fixture, "UART configuration rejects absent and unreadable devices",
                 "[console][hal]") {
  size_t transferred = 0;
  fake.present = false;
  REQUIRE(HAL_CONSOLE_Write("x", 1, transferred) ==
          kernel::KernelError::DeviceNotReady);
  REQUIRE(transferred == 0);
  REQUIRE(fake.output.empty());
  REQUIRE(fake.lsr_reads == 1);

  fake = {};
  fake.registers[3] = 0x03;
  fake.fail_divisor_write = true;
  output = hal::x86::detail::ConsoleOutput(
      {nullptr, ReadPort, WritePort},
      {nullptr, SaveAndDisable, RestoreInterrupts});
  REQUIRE(HAL_CONSOLE_Write("x", 1, transferred) ==
          kernel::KernelError::DeviceNotReady);
  REQUIRE(transferred == 0);
  REQUIRE(fake.output.empty());
  REQUIRE(fake.interrupts_enabled);
}

TEST_CASE_METHOD(Fixture, "HAL console validates arguments and caps each write",
                 "[console][hal]") {
  size_t transferred = 17;
  REQUIRE(HAL_CONSOLE_Write(nullptr, 1, transferred) ==
          kernel::KernelError::InvalidArgument);
  REQUIRE(transferred == 0);
  const size_t reads_before_empty = fake.lsr_reads;
  REQUIRE_FALSE(HAL_CONSOLE_Write("", 0, transferred));
  REQUIRE(transferred == 0);
  REQUIRE(fake.lsr_reads == reads_before_empty);

  const std::string large(5000, 'z');
  REQUIRE(HAL_CONSOLE_Write(large.data(), large.size(), transferred) ==
          kernel::KernelError::BufferTooSmall);
  REQUIRE(transferred == 4096);
  REQUIRE(fake.output == large.substr(0, 4096));
}

TEST_CASE_METHOD(Fixture, "UART polling times out with exact partial counts",
                 "[console][hal]") {
  size_t transferred = 0;
  fake.polls_before_ready = 65536;
  REQUIRE(HAL_CONSOLE_Write("x", 1, transferred) == kernel::KernelError::Timeout);
  REQUIRE(transferred == 0);
  REQUIRE(fake.lsr_reads == 65538);
  REQUIRE(fake.output.empty());

  fake = {};
  fake.registers[3] = 0x03;
  fake.polls_before_ready = 0;
  output = hal::x86::detail::ConsoleOutput(
      {nullptr, ReadPort, WritePort},
      {nullptr, SaveAndDisable, RestoreInterrupts});
  REQUIRE_FALSE(HAL_CONSOLE_Write("ab", 2, transferred));
  fake.polls_before_ready = 65536;
  REQUIRE(HAL_CONSOLE_Write("cd", 2, transferred) ==
          kernel::KernelError::Timeout);
  REQUIRE(transferred == 0);
  REQUIRE(fake.output == "ab");
}

TEST_CASE_METHOD(Fixture, "UART bounds total LSR reads for one call",
                 "[console][hal]") {
  size_t transferred = 0;
  fake.polls_before_ready = 255;
  const std::string data(4096, 'x');
  REQUIRE(HAL_CONSOLE_Write(data.data(), data.size(), transferred) ==
          kernel::KernelError::Timeout);
  REQUIRE(transferred == 4095);
  REQUIRE(fake.lsr_reads == 1048576);
}

TEST_CASE_METHOD(Fixture, "Emergency output recovers once without a normal lock",
                 "[console][hal]") {
  size_t transferred = 0;
  REQUIRE_FALSE(HAL_CONSOLE_Write("normal", 6, transferred));
  fake.registers[3] = 0x80;
  fake.interrupts_enabled = false;

  const auto irq_saves_before = fake.irq_saves;
  REQUIRE_FALSE(HAL_CONSOLE_WriteEmergency("panic", 5, transferred));
  REQUIRE(transferred == 5);
  REQUIRE(fake.fifo_writes == 2);
  REQUIRE(fake.output == "normalpanic");
  REQUIRE(fake.irq_saves == irq_saves_before + 1);
  REQUIRE(fake.irq_restores == fake.irq_saves);
  REQUIRE_FALSE(fake.interrupts_enabled);

  REQUIRE_FALSE(HAL_CONSOLE_WriteEmergency("again", 5, transferred));
  REQUIRE(fake.fifo_writes == 2);
  REQUIRE(fake.output == "normalpanicagain");
}
