#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hal::x86 {

void begin_hardware_interrupt() noexcept;
void end_hardware_interrupt() noexcept;

struct ReflectedIrqFrame {
  uint16_t stack;
  uint16_t cs;
  uint16_t ip;
};

class V86IrqTracking {
 public:
  enum class DispatchKind : uint8_t { HardwareIrq, SoftwareInt };

  void begin_dispatch(DispatchKind kind) noexcept {
    if (kind == DispatchKind::HardwareIrq) begin_hardware_interrupt();
  }

  void end_dispatch(DispatchKind kind) noexcept {
    if (kind == DispatchKind::HardwareIrq) end_hardware_interrupt();
  }

  bool begin_hardware_dispatch(bool virtual_if_enabled, uint32_t irq) noexcept {
    begin_dispatch(DispatchKind::HardwareIrq);
    if (!virtual_if_enabled) set_pending(irq);
    return virtual_if_enabled;
  }

  void end_hardware_dispatch() noexcept {
    end_dispatch(DispatchKind::HardwareIrq);
  }

  void set_pending(uint32_t irq) noexcept { pending_irq_ = irq; }

  bool take_pending(uint32_t &irq) noexcept {
    if (pending_irq_ == NO_PENDING_IRQ) return false;
    irq = pending_irq_;
    pending_irq_ = NO_PENDING_IRQ;
    return true;
  }

  bool begin_reflected(ReflectedIrqFrame frame) noexcept {
    if (reflected_depth_ == MAX_REFLECTED_IRQS) return false;
    begin_hardware_interrupt();
    reflected_[reflected_depth_++] = frame;
    return true;
  }

  bool complete_iret(ReflectedIrqFrame frame) noexcept {
    if (!reflected_depth_) return false;
    const auto &active = reflected_[reflected_depth_ - 1];
    if (frame.stack != active.stack || frame.cs != active.cs ||
        frame.ip != active.ip)
      return false;
    --reflected_depth_;
    end_hardware_interrupt();
    return true;
  }

  void complete_session() noexcept {
    while (reflected_depth_) {
      --reflected_depth_;
      end_hardware_interrupt();
    }
    pending_irq_ = NO_PENDING_IRQ;
  }

  size_t reflected_depth() const noexcept { return reflected_depth_; }

 private:
  static constexpr uint32_t NO_PENDING_IRQ = UINT32_MAX;
  static constexpr size_t MAX_REFLECTED_IRQS = 32;

  ReflectedIrqFrame reflected_[MAX_REFLECTED_IRQS]{};
  size_t reflected_depth_{};
  uint32_t pending_irq_{NO_PENDING_IRQ};
};

}// namespace hal::x86
