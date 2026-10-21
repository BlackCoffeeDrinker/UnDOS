
#include "gameport_hid_report.hpp"
#include "hid_keyboard_usages.hpp"
#include "hid_parser.hpp"
#include "ps2_hid_reports.hpp"

#include <kernel/device.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Exercises the shared HID class driver end to end against the two synthetic
// legacy descriptors, so the parser is proven long before any USB hardware
// exists. KE_INPUT_PushEvent is stubbed here to capture what SubmitReport
// produces -- the real one lives in kernel/src/input/input_manager.cpp, which
// is out of scope for hosted tests.
namespace {

std::vector<kernel::input::KInputReport> g_pushed;
kernel::KDevice g_device;

kernel::KDevicePtr<kernel::KDevice> TestDevice() { return kernel::KDevicePtr<kernel::KDevice>(&g_device); }

void ResetCapture() { g_pushed.clear(); }

const kernel::hid::ReportField *FindField(const kernel::hid::ReportFormat &format,
                                          uint16_t usagePage, uint16_t usage) {
  for (size_t i = 0; i < format.fieldCount; ++i) {
    if (format.fields[i].usagePage == usagePage && format.fields[i].usage == usage) {
      return &format.fields[i];
    }
  }
  return nullptr;
}

}// namespace

UNDOS_KERNEL_API_DEF kstd::error_code KE_INPUT_PushEvent(const kernel::KDevicePtr<kernel::KDevice> &device,
                                                            const kernel::input::KInputReport &report) noexcept {
  (void) device;
  g_pushed.push_back(report);
  return kernel::KernelError::Success;
}

using namespace kernel::hid;
using namespace kernel::input;

TEST_CASE("ParseReportDescriptor accepts the synthetic keyboard descriptor", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kKeyboardReportDescriptor, sizeof(ps2::kKeyboardReportDescriptor)), format));

  REQUIRE(format.collectionUsage == usage_gd::Keyboard);
  REQUIRE(format.reportBytes == ps2::kKeyReportBytes);
  REQUIRE(format.fieldCount == 8 + ps2::kKeySlots);

  // Eight single-bit modifier fields first, then the array slots.
  for (size_t i = 0; i < 8; ++i) {
    REQUIRE(format.fields[i].bitSize == 1);
    REQUIRE(format.fields[i].bitOffset == i);
    REQUIRE_FALSE(format.fields[i].isArray);
    REQUIRE(format.fields[i].usage == usage_kbd::LeftControl + i);
  }
  for (size_t i = 0; i < ps2::kKeySlots; ++i) {
    const ReportField &field = format.fields[8 + i];
    REQUIRE(field.isArray);
    REQUIRE(field.bitSize == 8);
    REQUIRE(field.bitOffset == 16 + i * 8);// after the modifier byte and the reserved byte
  }
}

TEST_CASE("ParseReportDescriptor accepts the synthetic mouse descriptor", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kMouseReportDescriptor, sizeof(ps2::kMouseReportDescriptor)), format));

  REQUIRE(format.collectionUsage == usage_gd::Mouse);
  REQUIRE(format.reportBytes == ps2::kMouseReportBytes);
  REQUIRE(format.fieldCount == 5 + 3);// 5 buttons, X, Y, Wheel

  const ReportField *x = FindField(format, usage_page::GenericDesktop, usage_gd::X);
  REQUIRE(x != nullptr);
  REQUIRE(x->isRelative);
  REQUIRE(x->bitSize == 8);
  REQUIRE(x->logicalMin == -127);
  REQUIRE(x->logicalMax == 127);

  const ReportField *button1 = FindField(format, usage_page::Button, 1);
  REQUIRE(button1 != nullptr);
  REQUIRE(button1->bitSize == 1);
  REQUIRE_FALSE(button1->isRelative);
}

TEST_CASE("ParseReportDescriptor accepts the synthetic joystick descriptor", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(gameport::kJoystickReportDescriptor, sizeof(gameport::kJoystickReportDescriptor)),
      format));

  REQUIRE(format.collectionUsage == usage_gd::Joystick);
  REQUIRE(format.reportBytes == gameport::kReportBytes);
  REQUIRE(format.fieldCount == 4 + 4);

  const ReportField *rx = FindField(format, usage_page::GenericDesktop, usage_gd::Rx);
  REQUIRE(rx != nullptr);
  REQUIRE(rx->logicalMin == 0);
  REQUIRE(rx->logicalMax == 255);
  REQUIRE_FALSE(rx->isRelative);
}

TEST_CASE("ParseReportDescriptor rejects malformed descriptors", "[hid_parser]") {
  ReportFormat format;

  // Item header promising more data than the buffer holds.
  const uint8_t truncated[] = {0x05};
  REQUIRE(ParseReportDescriptor(kstd::span<const uint8_t>(truncated, sizeof(truncated)), format));

  // Collection opened but never closed.
  const uint8_t unterminated[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02};
  REQUIRE(ParseReportDescriptor(kstd::span<const uint8_t>(unterminated, sizeof(unterminated)), format));

  // End Collection with no matching Collection.
  const uint8_t stray[] = {0xC0};
  REQUIRE(ParseReportDescriptor(kstd::span<const uint8_t>(stray, sizeof(stray)), format));

  // A long item is rejected rather than skipped.
  const uint8_t longItem[] = {0xFE, 0x00, 0x00};
  REQUIRE(ParseReportDescriptor(kstd::span<const uint8_t>(longItem, sizeof(longItem)), format));
}

TEST_CASE("ExtractField sign-extends only signed fields", "[hid_parser]") {
  const uint8_t report[] = {0x00, 0x80, 0x80, 0x00};

  const ReportField signedField{.usagePage = usage_page::GenericDesktop, .usage = usage_gd::X, .bitOffset = 8, .bitSize = 8, .isRelative = true, .isArray = false, .logicalMin = -127, .logicalMax = 127};
  REQUIRE(ExtractField(signedField, kstd::span<const uint8_t>(report, sizeof(report))) == -128);

  const ReportField unsignedField{.usagePage = usage_page::GenericDesktop, .usage = usage_gd::X, .bitOffset = 8, .bitSize = 8, .isRelative = false, .isArray = false, .logicalMin = 0, .logicalMax = 255};
  REQUIRE(ExtractField(unsignedField, kstd::span<const uint8_t>(report, sizeof(report))) == 128);
}

TEST_CASE("SubmitReport rejects a report shorter than the format", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kMouseReportDescriptor, sizeof(ps2::kMouseReportDescriptor)), format));

  ResetCapture();
  DecoderState state;
  const uint8_t shortReport[] = {0x00, 0x01};
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(shortReport, sizeof(shortReport)));
  REQUIRE(g_pushed.empty());
}

TEST_CASE("SubmitReport round-trips a packed mouse report", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kMouseReportDescriptor, sizeof(ps2::kMouseReportDescriptor)), format));

  MousePacket packet;
  packet.dx = -12;
  packet.dy = 34;
  packet.wheel = -1;
  packet.buttons = mouse_buttons::Left | mouse_buttons::Button5;

  uint8_t report[ps2::kMouseReportBytes]{};
  ps2::PackMouseReport(packet, report);

  ResetCapture();
  DecoderState state;
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));

  REQUIRE(g_pushed.size() == 1);
  REQUIRE(g_pushed[0].kind == InputReportKind::Pointer);
  REQUIRE(g_pushed[0].pointer.dx == -12);
  REQUIRE(g_pushed[0].pointer.dy == 34);
  REQUIRE(g_pushed[0].pointer.wheel == -1);
  REQUIRE(g_pushed[0].pointer.buttons == (mouse_buttons::Left | mouse_buttons::Button5));
  REQUIRE_FALSE(g_pushed[0].pointer.isAbsolute);
}

TEST_CASE("SubmitReport emits one make and one break for a held key", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kKeyboardReportDescriptor, sizeof(ps2::kKeyboardReportDescriptor)), format));

  DecoderState state;
  ps2::KeyboardReportBuilder builder;
  uint8_t report[ps2::kKeyReportBytes]{};

  ResetCapture();
  builder.Apply(ps2::KeyTransition{.usage = usage_kbd::A, .isBreak = false});
  builder.Pack(report);
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));
  REQUIRE(g_pushed.size() == 1);
  REQUIRE(g_pushed[0].kind == InputReportKind::Key);
  REQUIRE(g_pushed[0].key.virtualKey == vk::VK_A);
  REQUIRE(g_pushed[0].key.character == 'a');
  REQUIRE((g_pushed[0].key.keyFlags & key_flags::Break) == 0);

  // The same report again (the key is still held) must produce nothing.
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));
  REQUIRE(g_pushed.size() == 1);

  ResetCapture();
  builder.Apply(ps2::KeyTransition{.usage = usage_kbd::A, .isBreak = true});
  builder.Pack(report);
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));
  REQUIRE(g_pushed.size() == 1);
  REQUIRE((g_pushed[0].key.keyFlags & key_flags::Break) != 0);
}

TEST_CASE("SubmitReport scales joystick axes into the normalized range", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(gameport::kJoystickReportDescriptor, sizeof(gameport::kJoystickReportDescriptor)),
      format));

  // Minimum, centre, maximum, centre; buttons 1 and 3 pressed (active high
  // by the time the report is packed).
  const uint8_t report[] = {0x00, 0x80, 0xFF, 0x80, 0x05};

  ResetCapture();
  DecoderState state;
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));

  REQUIRE(g_pushed.size() == 1);
  REQUIRE(g_pushed[0].kind == InputReportKind::Gamepad);
  const GamepadState &pad = g_pushed[0].gamepad;
  REQUIRE(pad.axes[gamepad_axis::LeftX] == GamepadState::kAxisMin);
  REQUIRE(pad.axes[gamepad_axis::RightX] == GamepadState::kAxisMax);
  REQUIRE(pad.axes[gamepad_axis::LeftY] > -1000);
  REQUIRE(pad.axes[gamepad_axis::LeftY] < 1000);
  REQUIRE(pad.buttons == 0x5u);
}

TEST_CASE("HID usage tables reproduce the original VK and ASCII mapping", "[hid_parser]") {
  // Letters: case follows Shift XOR CapsLock, exactly as the PS/2 translator
  // used to derive it.
  REQUIRE(MakeKeyEvent(usage_kbd::A, false, 0).virtualKey == vk::VK_A);
  REQUIRE(MakeKeyEvent(usage_kbd::A, false, 0).character == 'a');
  REQUIRE(MakeKeyEvent(usage_kbd::A, false, modifier_flags::LShift).character == 'A');
  REQUIRE(MakeKeyEvent(usage_kbd::A, false, modifier_flags::CapsLock).character == 'A');
  REQUIRE(MakeKeyEvent(usage_kbd::A, false, modifier_flags::LShift | modifier_flags::CapsLock).character == 'a');

  // Digit row: CapsLock does not apply, Shift does.
  REQUIRE(MakeKeyEvent(usage_kbd::Num1, false, 0).character == '1');
  REQUIRE(MakeKeyEvent(usage_kbd::Num1, false, modifier_flags::LShift).character == '!');
  REQUIRE(MakeKeyEvent(usage_kbd::Num1, false, modifier_flags::CapsLock).character == '1');

  // Keypad digits are gated on NumLock; the navigation cluster never has a
  // character and is flagged Extended.
  REQUIRE(MakeKeyEvent(usage_kbd::Kp1 + 6, false, 0).character == 0);
  REQUIRE(MakeKeyEvent(usage_kbd::Kp1 + 6, false, modifier_flags::NumLock).character == '7');
  REQUIRE(MakeKeyEvent(usage_kbd::Home, false, modifier_flags::NumLock).character == 0);
  REQUIRE(MakeKeyEvent(usage_kbd::Home, false, 0).virtualKey == vk::VK_HOME);
  REQUIRE((MakeKeyEvent(usage_kbd::Home, false, 0).keyFlags & key_flags::Extended) != 0);

  // Keypad operators ignore NumLock entirely.
  REQUIRE(MakeKeyEvent(usage_kbd::KpMultiply, false, 0).character == '*');
  REQUIRE(MakeKeyEvent(usage_kbd::KpPlus, false, 0).character == '+');
  REQUIRE(MakeKeyEvent(usage_kbd::KpDivide, false, 0).character == '/');

  // A break event never carries a character.
  REQUIRE(MakeKeyEvent(usage_kbd::A, true, 0).character == 0);

  // scanCode now carries the HID usage itself.
  REQUIRE(MakeKeyEvent(usage_kbd::Escape, false, 0).scanCode == usage_kbd::Escape);
  REQUIRE(MakeKeyEvent(usage_kbd::Escape, false, 0).virtualKey == vk::VK_ESCAPE);
}

TEST_CASE("SubmitReport tracks lock keys and held modifiers", "[hid_parser]") {
  ReportFormat format;
  REQUIRE_FALSE(ParseReportDescriptor(
      kstd::span<const uint8_t>(ps2::kKeyboardReportDescriptor, sizeof(ps2::kKeyboardReportDescriptor)), format));

  DecoderState state;
  ps2::KeyboardReportBuilder builder;
  uint8_t report[ps2::kKeyReportBytes]{};

  // CapsLock make toggles the lock bit on, and the following letter is
  // uppercased by it.
  builder.Apply(ps2::KeyTransition{.usage = usage_kbd::CapsLock, .isBreak = false});
  builder.Pack(report);
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));
  builder.Apply(ps2::KeyTransition{.usage = usage_kbd::CapsLock, .isBreak = true});
  builder.Pack(report);
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));

  ResetCapture();
  builder.Apply(ps2::KeyTransition{.usage = usage_kbd::A, .isBreak = false});
  builder.Pack(report);
  SubmitReport(TestDevice(), format, state, kstd::span<const uint8_t>(report, sizeof(report)));
  REQUIRE(g_pushed.size() == 1);
  REQUIRE(g_pushed[0].key.character == 'A');
  REQUIRE((g_pushed[0].key.modifiers & modifier_flags::CapsLock) != 0);
}
