#pragma once

#include <kernel/dispatch.hpp>
#include <vector>

namespace kernel::dispatch {
class DpcCore;
}

namespace test_dispatch {

// Observable fake-HAL event stream for ordering assertions (callback ran,
// source completed). Entries are appended by the HAL fakes and by tests via
// RecordCallbackEvent.
struct FakeHalEvent {
  enum Kind { Callback, Complete };
  Kind kind;
  uint32_t irq;
};

struct FakeHalState {
  bool failNextConnect{false};
  uint32_t connectCount{0};
  uint32_t disconnectCount{0};
  uint32_t completeCount{0};
  uint32_t lastConnectedIrq{0};
  uint32_t lastDisconnectedIrq{0};
  uint32_t lastCompletedIrq{0};
  std::vector<FakeHalEvent> events;
};

FakeHalState &FakeHal() noexcept;

kernel::dispatch::DpcCore &Core() noexcept;

void RecordCallbackEvent(uint32_t irq) noexcept;

void Reset() noexcept;

void Pump() noexcept;

} // namespace test_dispatch
