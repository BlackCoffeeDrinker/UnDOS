#pragma once

#include <Kernel.hpp>

namespace hal::x86::detail {

struct PortIo {
  void *context;
  uint8_t (*read)(void *, uint16_t) noexcept;
  void (*write)(void *, uint16_t, uint8_t) noexcept;
};

struct InterruptOps {
  void *context;
  uintptr_t (*save_and_disable)(void *) noexcept;
  void (*restore)(void *, uintptr_t) noexcept;
};

class PolledUart {
 public:
  constexpr explicit PolledUart(PortIo io,
                                uint16_t base = 0x3F8) noexcept
      : io_(io), base_(base) {}

  kstd::error_code Write(const char *text, size_t size,
                         size_t &transferred, bool emergency) noexcept {
    transferred = 0;
    if (!text && size) return kernel::KernelError::InvalidArgument;
    if (!size) return kernel::KernelError::Success;

    size_t lsr_reads = 0;
    if (emergency && !emergency_reconfigured_) {
      emergency_reconfigured_ = true;
      initialized_ = false;
      if (!Configure(lsr_reads)) return kernel::KernelError::DeviceNotReady;
      initialized_ = true;
    } else if (!initialized_) {
      if (!Configure(lsr_reads)) return kernel::KernelError::DeviceNotReady;
      initialized_ = true;
    } else if (!ConfigurationMatches()) {
      return kernel::KernelError::DeviceNotReady;
    }

    const size_t accepted = size < MaxWrite ? size : MaxWrite;
    for (size_t i = 0; i < accepted; ++i) {
      size_t byte_reads = 0;
      for (;;) {
        if (lsr_reads >= MaxReadsPerCall ||
            byte_reads >= MaxReadsPerByte) {
          return kernel::KernelError::Timeout;
        }
        ++lsr_reads;
        ++byte_reads;
        const uint8_t status = io_.read(io_.context, base_ + Lsr);
        if (status == 0xFF) return kernel::KernelError::DeviceNotReady;
        if (status & 0x20) break;
      }
      io_.write(io_.context, base_ + Data,
                static_cast<uint8_t>(text[i]));
      ++transferred;
    }
    return accepted == size ? kernel::KernelError::Success
                            : kernel::KernelError::BufferTooSmall;
  }

 private:
  static constexpr uint16_t Data = 0;
  static constexpr uint16_t Ier = 1;
  static constexpr uint16_t Fcr = 2;
  static constexpr uint16_t Lcr = 3;
  static constexpr uint16_t Mcr = 4;
  static constexpr uint16_t Lsr = 5;
  static constexpr size_t MaxWrite = 4096;
  static constexpr size_t MaxReadsPerByte = 65536;
  static constexpr size_t MaxReadsPerCall = 1048576;

  bool ReadPresent(size_t &lsr_reads) noexcept {
    if (lsr_reads >= MaxReadsPerCall) return false;
    ++lsr_reads;
    return io_.read(io_.context, base_ + Lsr) != 0xFF;
  }

  bool Configure(size_t &lsr_reads) noexcept {
    if (!ReadPresent(lsr_reads)) return false;

    io_.write(io_.context, base_ + Ier, 0x00);
    io_.write(io_.context, base_ + Lcr, 0x80);
    io_.write(io_.context, base_ + Data, 0x01);
    io_.write(io_.context, base_ + Ier, 0x00);
    if (io_.read(io_.context, base_ + Data) != 0x01 ||
        io_.read(io_.context, base_ + Ier) != 0x00) {
      io_.write(io_.context, base_ + Lcr, 0x03);
      return false;
    }
    io_.write(io_.context, base_ + Lcr, 0x03);
    io_.write(io_.context, base_ + Fcr, 0xC7);
    io_.write(io_.context, base_ + Mcr, 0x03);
    if (!ReadPresent(lsr_reads)) return false;
    return ConfigurationMatches();
  }

  bool ConfigurationMatches() noexcept {
    return io_.read(io_.context, base_ + Ier) == 0x00 &&
           io_.read(io_.context, base_ + Lcr) == 0x03 &&
           io_.read(io_.context, base_ + Mcr) == 0x03;
  }

  PortIo io_;
  uint16_t base_;
  bool initialized_ = false;
  bool emergency_reconfigured_ = false;
};

class ConsoleOutput {
 public:
  constexpr ConsoleOutput(PortIo io, InterruptOps interrupts) noexcept
      : uart_(io), interrupts_(interrupts) {}

  kstd::error_code WriteNormal(const char *text, size_t size,
                               size_t &transferred) noexcept {
    const uintptr_t state =
        interrupts_.save_and_disable(interrupts_.context);
    const auto result = uart_.Write(text, size, transferred, false);
    interrupts_.restore(interrupts_.context, state);
    return result;
  }

  kstd::error_code WriteEmergency(const char *text, size_t size,
                                  size_t &transferred) noexcept {
    const uintptr_t state =
        interrupts_.save_and_disable(interrupts_.context);
    const auto result = uart_.Write(text, size, transferred, true);
    interrupts_.restore(interrupts_.context, state);
    return result;
  }

 private:
  PolledUart uart_;
  InterruptOps interrupts_;
};

kstd::error_code WriteSerialConsole(const char *text, size_t size,
                                    size_t &transferred) noexcept;
kstd::error_code WriteSerialConsoleEmergency(const char *text, size_t size,
                                             size_t &transferred) noexcept;

}
