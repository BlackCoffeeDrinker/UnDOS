#pragma once

#include <array.hpp>
#include <stdint.h>

#include "hid_usage_tables.hpp"

// PS/2 scancode set 1 -> HID Keyboard/Keypad (page 0x07) usage tables. The
// driver's whole knowledge of the wire protocol ends here: what a usage
// *means* (virtual key, ASCII, modifier) is answered once, by
// drivers/input_hid/hid_keyboard_usages.hpp, for every keyboard transport.
// Header-only, freestanding, no kernel dependencies, so it compiles
// unchanged into KernelTests (see hosted_test/tests/input_scancode_test.cpp).
namespace ps2 {

using kernel::hid::usage_kbd::None;

namespace detail {

constexpr uint16_t Letter(char c) noexcept {
  return static_cast<uint16_t>(kernel::hid::usage_kbd::A + (c - 'A'));
}

constexpr kstd::array<uint16_t, 128> BuildBaseTable() noexcept {
  using namespace kernel::hid::usage_kbd;
  kstd::array<uint16_t, 128> t{};
  for (auto &v: t.elements) v = None;

  t[0x01] = Escape;
  for (int i = 0; i < 9; ++i) t[0x02 + i] = static_cast<uint16_t>(Num1 + i);
  t[0x0B] = Num0;
  t[0x0C] = Minus;
  t[0x0D] = Equal;
  t[0x0E] = Backspace;
  t[0x0F] = Tab;
  t[0x10] = Letter('Q');
  t[0x11] = Letter('W');
  t[0x12] = Letter('E');
  t[0x13] = Letter('R');
  t[0x14] = Letter('T');
  t[0x15] = Letter('Y');
  t[0x16] = Letter('U');
  t[0x17] = Letter('I');
  t[0x18] = Letter('O');
  t[0x19] = Letter('P');
  t[0x1A] = LeftBracket;
  t[0x1B] = RightBracket;
  t[0x1C] = Enter;
  t[0x1D] = LeftControl;
  t[0x1E] = Letter('A');
  t[0x1F] = Letter('S');
  t[0x20] = Letter('D');
  t[0x21] = Letter('F');
  t[0x22] = Letter('G');
  t[0x23] = Letter('H');
  t[0x24] = Letter('J');
  t[0x25] = Letter('K');
  t[0x26] = Letter('L');
  t[0x27] = Semicolon;
  t[0x28] = Quote;
  t[0x29] = Grave;
  t[0x2A] = LeftShift;
  t[0x2B] = Backslash;
  t[0x2C] = Letter('Z');
  t[0x2D] = Letter('X');
  t[0x2E] = Letter('C');
  t[0x2F] = Letter('V');
  t[0x30] = Letter('B');
  t[0x31] = Letter('N');
  t[0x32] = Letter('M');
  t[0x33] = Comma;
  t[0x34] = Period;
  t[0x35] = Slash;
  t[0x36] = RightShift;
  t[0x37] = KpMultiply;
  t[0x38] = LeftAlt;
  t[0x39] = Space;
  t[0x3A] = CapsLock;
  for (int i = 0; i < 10; ++i) t[0x3B + i] = static_cast<uint16_t>(F1 + i);// F1..F10
  t[0x45] = NumLock;
  t[0x46] = ScrollLock;
  t[0x47] = static_cast<uint16_t>(Kp1 + 6);// KP 7
  t[0x48] = static_cast<uint16_t>(Kp1 + 7);// KP 8
  t[0x49] = static_cast<uint16_t>(Kp1 + 8);// KP 9
  t[0x4A] = KpMinus;
  t[0x4B] = static_cast<uint16_t>(Kp1 + 3);// KP 4
  t[0x4C] = static_cast<uint16_t>(Kp1 + 4);// KP 5
  t[0x4D] = static_cast<uint16_t>(Kp1 + 5);// KP 6
  t[0x4E] = KpPlus;
  t[0x4F] = Kp1;
  t[0x50] = static_cast<uint16_t>(Kp1 + 1);// KP 2
  t[0x51] = static_cast<uint16_t>(Kp1 + 2);// KP 3
  t[0x52] = Kp0;
  t[0x53] = KpPeriod;
  t[0x57] = static_cast<uint16_t>(F1 + 10);// F11
  t[0x58] = static_cast<uint16_t>(F1 + 11);// F12

  return t;
}

}// namespace detail

// Base (non-extended) scancode set 1, indexed by the scancode with the
// break bit (0x80) masked off. `None` for unmapped/unused codes.
constexpr kstd::array<uint16_t, 128> kSet1BaseToUsage = detail::BuildBaseTable();

// Sparse E0-prefixed extended scancode -> usage (navigation cluster,
// RCtrl/RAlt, KP-Enter, KP-/, Win/Apps, PrtScn, Ctrl+Break).
constexpr uint16_t LookupExtendedUsage(uint8_t scanCode) noexcept {
  using namespace kernel::hid::usage_kbd;
  switch (scanCode) {
    case 0x1C: return KpEnter;
    case 0x1D: return RightControl;
    case 0x35: return KpDivide;
    case 0x37: return PrintScreen;
    case 0x38: return RightAlt;
    case 0x46: return Cancel;// Ctrl+Break (E0 46)
    case 0x47: return Home;
    case 0x48: return Up;
    case 0x49: return PageUp;
    case 0x4B: return Left;
    case 0x4D: return Right;
    case 0x4F: return End;
    case 0x50: return Down;
    case 0x51: return PageDown;
    case 0x52: return Insert;
    case 0x53: return Delete;
    case 0x5B: return LeftGui;
    case 0x5C: return RightGui;
    case 0x5D: return Application;
    default: return None;
  }
}

}// namespace ps2
