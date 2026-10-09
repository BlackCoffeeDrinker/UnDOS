
#include "gameport_hid_report.hpp"

#include <kernel/input.hpp>

#include <catch2/catch_test_macros.hpp>

// The gameport's port-free arithmetic: raw discharge counts -> the 0..255
// range its descriptor declares, and active-low buttons -> HID order. The
// final scaling into GamepadState's normalized range is the shared HID
// layer's job and is asserted in hid_parser_test.cpp.
using namespace kernel::input;

TEST_CASE("ScaleAxis maps minimum, centre and maximum deflection", "[gamepad]") {
  uint16_t axisMax = 1000;

  REQUIRE(gameport::ScaleAxis(0, axisMax) == 0);
  REQUIRE(gameport::ScaleAxis(1000, axisMax) == 255);

  const uint8_t centre = gameport::ScaleAxis(500, axisMax);
  REQUIRE(centre > 120);
  REQUIRE(centre < 136);
}

TEST_CASE("ScaleAxis widens its range instead of over-ranging", "[gamepad]") {
  uint16_t axisMax = 1000;

  // A count past the current maximum widens the range rather than producing
  // a value above the declared logical maximum.
  REQUIRE(gameport::ScaleAxis(4000, axisMax) == 255);
  REQUIRE(axisMax == 4000);

  // The previously-full-scale count is now mid-range against the wider span.
  REQUIRE(gameport::ScaleAxis(1000, axisMax) < 255);
  REQUIRE(axisMax == 4000);
}

TEST_CASE("InvertButtons turns active-low port bits into HID order", "[gamepad]") {
  // Nothing pressed: all four button bits read high.
  REQUIRE(gameport::InvertButtons(0xF0) == 0x0);
  // Everything pressed: all four read low.
  REQUIRE(gameport::InvertButtons(0x0F) == 0xF);
  // Button 1 only (bit 4 low).
  REQUIRE(gameport::InvertButtons(0xE0) == 0x1);
  // Button 4 only (bit 7 low).
  REQUIRE(gameport::InvertButtons(0x70) == 0x8);
}

TEST_CASE("PackJoystickReport centres an axis whose one-shot never fired", "[gamepad]") {
  uint16_t axisMax[gameport_regs::kAxisCount] = {1000, 1000, 1000, 1000};
  gameport::Sample sample;
  sample.counts[0] = 1000;
  sample.axisValid[0] = true;
  sample.counts[1] = 0;
  sample.axisValid[1] = true;
  // Axes 2 and 3 are absent: no joystick on the second port.
  sample.rawButtons = 0xE0;// button 1 held

  uint8_t report[gameport::kReportBytes]{};
  gameport::PackJoystickReport(sample, axisMax, report);

  REQUIRE(report[0] == 255);
  REQUIRE(report[1] == 0);
  REQUIRE(report[2] == 0x80);// absent -> centred, not full deflection
  REQUIRE(report[3] == 0x80);
  REQUIRE(report[4] == 0x1);
}

TEST_CASE("KInputReport converting constructors never set a mismatched tag", "[gamepad]") {
  KeyboardInputEvent key;
  key.virtualKey = 0x41;
  const KInputReport keyReport{key};
  REQUIRE(keyReport.kind == InputReportKind::Key);
  REQUIRE(keyReport.key.virtualKey == 0x41);

  MousePacket pointer;
  pointer.dx = -5;
  pointer.isAbsolute = true;
  const KInputReport pointerReport{pointer};
  REQUIRE(pointerReport.kind == InputReportKind::Pointer);
  REQUIRE(pointerReport.pointer.dx == -5);
  REQUIRE(pointerReport.pointer.isAbsolute);

  GamepadState pad;
  pad.axes[gamepad_axis::LeftX] = GamepadState::kAxisMax;
  pad.buttons = 0b1010;
  const KInputReport padReport{pad};
  REQUIRE(padReport.kind == InputReportKind::Gamepad);
  REQUIRE(padReport.gamepad.axes[gamepad_axis::LeftX] == GamepadState::kAxisMax);
  REQUIRE(padReport.gamepad.buttons == 0b1010u);
}
