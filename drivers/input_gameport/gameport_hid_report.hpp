#pragma once

#include <stdint.h>

#include "gameport_registers.hpp"

// The analog gameport's synthetic HID face, plus the pure sample->report
// arithmetic. Both are freestanding and port-free so they can be unit
// tested without any I/O (see hosted_test/tests/gamepad_state_test.cpp).
namespace gameport {

constexpr size_t kReportBytes = 5;// 4 axis bytes + 1 button/padding byte

// 4-axis, 4-button Joystick collection. Axes are declared 0..255 and the
// shared HID layer scales them into GamepadState's normalized range, so the
// analog port's raw discharge counts end up directly comparable with a USB
// pad's 8-bit axes.
constexpr uint8_t kJoystickReportDescriptor[] = {
    0x05, 0x01,      // Usage Page (Generic Desktop)
    0x09, 0x04,      // Usage (Joystick)
    0xA1, 0x01,      // Collection (Application)
    0x09, 0x30,      //   Usage (X)
    0x09, 0x31,      //   Usage (Y)
    0x09, 0x33,      //   Usage (Rx)
    0x09, 0x34,      //   Usage (Ry)
    0x15, 0x00,      //   Logical Minimum (0)
    0x26, 0xFF, 0x00,//   Logical Maximum (255)
    0x75, 0x08,      //   Report Size (8)
    0x95, 0x04,      //   Report Count (4)
    0x81, 0x02,      //   Input (Data, Variable, Absolute)
    0x05, 0x09,      //   Usage Page (Button)
    0x19, 0x01,      //   Usage Minimum (Button 1)
    0x29, 0x04,      //   Usage Maximum (Button 4)
    0x15, 0x00,      //   Logical Minimum (0)
    0x25, 0x01,      //   Logical Maximum (1)
    0x75, 0x01,      //   Report Size (1)
    0x95, 0x04,      //   Report Count (4)
    0x81, 0x02,      //   Input (Data, Variable, Absolute)
    0x95, 0x01,      //   Report Count (1)
    0x75, 0x04,      //   Report Size (4)
    0x81, 0x03,      //   Input (Constant) -- padding
    0xC0,            // End Collection
};

// One raw sample: a discharge count per axis and the raw (active-low) port
// byte the buttons were read from.
struct Sample {
  uint16_t counts[gameport_regs::kAxisCount] = {};
  uint8_t rawButtons = 0;
  bool axisValid[gameport_regs::kAxisCount] = {};
};

// Scales a raw discharge count into the 0..255 range the descriptor
// declares, widening `axisMax` when a larger count is observed. `axisMax`
// starts unknown and is seeded from the standard timing constant, so the
// first few reports after attach may under-range rather than clipping.
constexpr uint8_t ScaleAxis(uint16_t count, uint16_t &axisMax) noexcept {
  if (count > axisMax) axisMax = count;
  if (axisMax == 0) return 0;
  const uint32_t scaled = (static_cast<uint32_t>(count) * 255u) / axisMax;
  return static_cast<uint8_t>(scaled > 255u ? 255u : scaled);
}

// Gameport button bits are active low: a clear bit means the button is
// pressed. Returns the HID-order (active-high) nibble.
constexpr uint8_t InvertButtons(uint8_t rawPortByte) noexcept {
  const uint8_t pressed = static_cast<uint8_t>(~rawPortByte) & gameport_regs::status::ButtonMask;
  return static_cast<uint8_t>(pressed >> 4);
}

inline void PackJoystickReport(const Sample &sample, uint16_t (&axisMax)[gameport_regs::kAxisCount],
                               uint8_t (&report)[kReportBytes]) noexcept {
  for (size_t i = 0; i < gameport_regs::kAxisCount; ++i) {
    // An axis whose one-shot never fired is absent, not at full deflection:
    // report it centred rather than pinned to an extreme.
    report[i] = sample.axisValid[i] ? ScaleAxis(sample.counts[i], axisMax[i]) : 0x80;
  }
  report[4] = InvertButtons(sample.rawButtons);
}

}// namespace gameport
