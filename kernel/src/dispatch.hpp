#pragma once

#include <kernel/dispatch.hpp>
#include <vector.hpp>

namespace kernel::dispatch {

void init() noexcept;

InterruptRecordRing &get_ring() noexcept;

class InterruptHandlerRegistry {
 public:
  static constexpr size_t kMaxHandlersPerInterrupt = 4;

  kstd::error_code Register(
      uint32_t interruptId,
      const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept {
    if (!isr) return KernelError::InvalidArgument;

    Slot *availableSlot = nullptr;
    for (auto &slot: m_slots) {
      if (slot.active && slot.interruptId == interruptId) {
        for (size_t i = 0; i < slot.count; ++i) {
          if (slot.handlers[i] == isr) return {};
        }
        if (slot.count >= kMaxHandlersPerInterrupt) return KernelError::ResourceLimitExceeded;
        slot.handlers[slot.count++] = isr;
        return {};
      }
      if (!slot.active && !availableSlot) availableSlot = &slot;
    }

    if (!availableSlot) {
      m_slots.emplace_back();
      availableSlot = &m_slots.back();
    }
    availableSlot->interruptId = interruptId;
    availableSlot->active = true;
    availableSlot->handlers[0] = isr;
    availableSlot->count = 1;
    return {};
  }

  void CopyHandlers(
      uint32_t interruptId,
      KObjectPtr<KInterruptServiceRoutineObject> (&handlers)[kMaxHandlersPerInterrupt],
      size_t &count) const noexcept {
    count = 0;
    for (const auto &slot: m_slots) {
      if (!slot.active || slot.interruptId != interruptId) continue;
      count = slot.count;
      for (size_t i = 0; i < count; ++i) {
        handlers[i] = slot.handlers[i];
      }
      return;
    }
  }

  kstd::error_code Unregister(uint32_t interruptId) noexcept {
    for (auto &slot: m_slots) {
      if (!slot.active || slot.interruptId != interruptId) continue;
      for (size_t i = 0; i < slot.count; ++i) {
        slot.handlers[i] = nullptr;
      }
      slot.count = 0;
      slot.active = false;
      return {};
    }
    return {};
  }

  void Reset() noexcept {
    m_slots.clear();
  }

 private:
  struct Slot {
    uint32_t interruptId{0};
    bool active{false};
    KObjectPtr<KInterruptServiceRoutineObject> handlers[kMaxHandlersPerInterrupt]{};
    size_t count{0};
  };

  kstd::vector<Slot> m_slots;
};
} // namespace kernel::dispatch
