#pragma once

#include <Kernel.hpp>

namespace serial_uart::detail {

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

class Uart {
 public:
  Uart(PortIo io, InterruptOps interrupts) noexcept
      : io_(io), interrupts_(interrupts) {}

  bool Start(uint16_t base) noexcept {
    if (base == Com1) return true;
    const uintptr_t state =
        interrupts_.save_and_disable(interrupts_.context);
    const bool ready = Configure(base);
    interrupts_.restore(interrupts_.context, state);
    return ready;
  }

  kstd::error_code Read(uint16_t base, uint8_t *buffer, size_t size,
                        size_t &transferred) noexcept {
    transferred = 0;
    if (!size) return kernel::KernelError::Success;
    for (; transferred < size;) {
      const uintptr_t state =
          interrupts_.save_and_disable(interrupts_.context);
      const uint8_t status = io_.read(io_.context, base + Lsr);
      if (status == 0xFF) {
        interrupts_.restore(interrupts_.context, state);
        return kernel::KernelError::DeviceNotReady;
      }
      if ((status & 0x01) == 0) {
        interrupts_.restore(interrupts_.context, state);
        break;
      }
      buffer[transferred++] = io_.read(io_.context, base + Data);
      interrupts_.restore(interrupts_.context, state);
    }
    return kernel::KernelError::Success;
  }

  kstd::error_code Write(uint16_t base, const uint8_t *buffer, size_t size,
                         size_t &transferred) noexcept {
    transferred = 0;
    if (!size) return kernel::KernelError::Success;
    for (; transferred < size;) {
      const uintptr_t state =
          interrupts_.save_and_disable(interrupts_.context);
      const uint8_t status = io_.read(io_.context, base + Lsr);
      if (status == 0xFF) {
        interrupts_.restore(interrupts_.context, state);
        return kernel::KernelError::DeviceNotReady;
      }
      if ((status & 0x20) == 0) {
        interrupts_.restore(interrupts_.context, state);
        break;
      }
      io_.write(io_.context, base + Data, buffer[transferred++]);
      interrupts_.restore(interrupts_.context, state);
    }
    return kernel::KernelError::Success;
  }

 private:
  static constexpr uint16_t Com1 = 0x3F8;
  static constexpr uint16_t Data = 0;
  static constexpr uint16_t Ier = 1;
  static constexpr uint16_t Fcr = 2;
  static constexpr uint16_t Lcr = 3;
  static constexpr uint16_t Mcr = 4;
  static constexpr uint16_t Lsr = 5;

  bool Configure(uint16_t base) noexcept {
    if (io_.read(io_.context, base + Lsr) == 0xFF) return false;
    io_.write(io_.context, base + Ier, 0x00);
    io_.write(io_.context, base + Lcr, 0x80);
    io_.write(io_.context, base + Data, 0x01);
    io_.write(io_.context, base + Ier, 0x00);
    if (io_.read(io_.context, base + Data) != 0x01 ||
        io_.read(io_.context, base + Ier) != 0x00) {
      io_.write(io_.context, base + Lcr, 0x03);
      return false;
    }
    io_.write(io_.context, base + Lcr, 0x03);
    io_.write(io_.context, base + Fcr, 0xC7);
    io_.write(io_.context, base + Mcr, 0x03);
    return io_.read(io_.context, base + Ier) == 0x00 &&
           io_.read(io_.context, base + Lcr) == 0x03 &&
           io_.read(io_.context, base + Mcr) == 0x03 &&
           io_.read(io_.context, base + Lsr) != 0xFF;
  }

  PortIo io_;
  InterruptOps interrupts_;
};

}
