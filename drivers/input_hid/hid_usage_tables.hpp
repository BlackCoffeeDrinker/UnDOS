#pragma once

#include <stdint.h>

// USB-IF HID Usage Tables, restricted to the pages this OS currently
// decodes: Generic Desktop (0x01), Keyboard/Keypad (0x07) and Button
// (0x09). Adding a page is purely additive -- nothing here is a UnDOS
// invention, so a real USB device and a synthetic legacy descriptor speak
// the same vocabulary. Header-only and freestanding.
namespace kernel::hid {

namespace usage_page {
constexpr uint16_t GenericDesktop = 0x01;
constexpr uint16_t Keyboard = 0x07;
constexpr uint16_t Button = 0x09;
}// namespace usage_page

// Generic Desktop page: the controls and the application-collection usages
// that tell the decoder which kind of device it is looking at.
namespace usage_gd {
constexpr uint16_t Pointer = 0x01;
constexpr uint16_t Mouse = 0x02;
constexpr uint16_t Joystick = 0x04;
constexpr uint16_t Gamepad = 0x05;
constexpr uint16_t Keyboard = 0x06;

constexpr uint16_t X = 0x30;
constexpr uint16_t Y = 0x31;
constexpr uint16_t Z = 0x32;
constexpr uint16_t Rx = 0x33;
constexpr uint16_t Ry = 0x34;
constexpr uint16_t Rz = 0x35;
constexpr uint16_t Wheel = 0x38;
constexpr uint16_t HatSwitch = 0x39;
}// namespace usage_gd

// Keyboard/Keypad page. Only the usages scancode set 1 can produce are
// named; the table is contiguous where the spec is contiguous (a..z,
// 1..0, F1..F12, keypad digits) so lookups stay arithmetic.
namespace usage_kbd {
constexpr uint16_t None = 0x00;
constexpr uint16_t A = 0x04;// A..Z contiguous, 0x04..0x1D
constexpr uint16_t Num1 = 0x1E;// 1..9 contiguous, 0x1E..0x26
constexpr uint16_t Num0 = 0x27;
constexpr uint16_t Enter = 0x28;
constexpr uint16_t Escape = 0x29;
constexpr uint16_t Backspace = 0x2A;
constexpr uint16_t Tab = 0x2B;
constexpr uint16_t Space = 0x2C;
constexpr uint16_t Minus = 0x2D;
constexpr uint16_t Equal = 0x2E;
constexpr uint16_t LeftBracket = 0x2F;
constexpr uint16_t RightBracket = 0x30;
constexpr uint16_t Backslash = 0x31;
constexpr uint16_t Semicolon = 0x33;
constexpr uint16_t Quote = 0x34;
constexpr uint16_t Grave = 0x35;
constexpr uint16_t Comma = 0x36;
constexpr uint16_t Period = 0x37;
constexpr uint16_t Slash = 0x38;
constexpr uint16_t CapsLock = 0x39;
constexpr uint16_t F1 = 0x3A;// F1..F12 contiguous, 0x3A..0x45
constexpr uint16_t PrintScreen = 0x46;
constexpr uint16_t ScrollLock = 0x47;
constexpr uint16_t Pause = 0x48;
constexpr uint16_t Insert = 0x49;
constexpr uint16_t Home = 0x4A;
constexpr uint16_t PageUp = 0x4B;
constexpr uint16_t Delete = 0x4C;
constexpr uint16_t End = 0x4D;
constexpr uint16_t PageDown = 0x4E;
constexpr uint16_t Right = 0x4F;
constexpr uint16_t Left = 0x50;
constexpr uint16_t Down = 0x51;
constexpr uint16_t Up = 0x52;
constexpr uint16_t NumLock = 0x53;
constexpr uint16_t KpDivide = 0x54;
constexpr uint16_t KpMultiply = 0x55;
constexpr uint16_t KpMinus = 0x56;
constexpr uint16_t KpPlus = 0x57;
constexpr uint16_t KpEnter = 0x58;
constexpr uint16_t Kp1 = 0x59;// Kp1..Kp9 contiguous, 0x59..0x61
constexpr uint16_t Kp0 = 0x62;
constexpr uint16_t KpPeriod = 0x63;
constexpr uint16_t Application = 0x65;// "Apps"/menu key
constexpr uint16_t Cancel = 0x9B;     // Ctrl+Break

constexpr uint16_t LeftControl = 0xE0;
constexpr uint16_t LeftShift = 0xE1;
constexpr uint16_t LeftAlt = 0xE2;
constexpr uint16_t LeftGui = 0xE3;
constexpr uint16_t RightControl = 0xE4;
constexpr uint16_t RightShift = 0xE5;
constexpr uint16_t RightAlt = 0xE6;
constexpr uint16_t RightGui = 0xE7;
}// namespace usage_kbd

// Bit positions of the HID boot-keyboard modifier byte; identical to
// usage_kbd::LeftControl..RightGui minus 0xE0.
namespace kbd_modifier_bit {
constexpr uint8_t LeftControl = 1 << 0;
constexpr uint8_t LeftShift = 1 << 1;
constexpr uint8_t LeftAlt = 1 << 2;
constexpr uint8_t LeftGui = 1 << 3;
constexpr uint8_t RightControl = 1 << 4;
constexpr uint8_t RightShift = 1 << 5;
constexpr uint8_t RightAlt = 1 << 6;
constexpr uint8_t RightGui = 1 << 7;
}// namespace kbd_modifier_bit

}// namespace kernel::hid
