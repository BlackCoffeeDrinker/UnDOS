#pragma once

#include <array.hpp>
#include <stdint.h>

#include <kernel/input.hpp>

#include "hid_usage_tables.hpp"

// HID Keyboard/Keypad (page 0x07) usage -> Win32 virtual-key code and
// US-ASCII, the one place in the system that answers "what does this key
// mean". A PS/2 scancode reaches this table only after the i8042 driver has
// turned it into a usage, so a USB keyboard and a PS/2 keyboard share a
// single mapping. Header-only, freestanding, no kernel calls.
namespace kernel::hid {

// Win32-compatible virtual-key codes for the subset of keys decoded here.
// VK_NONE (0) means "no virtual key for this usage".
namespace vk {
constexpr uint16_t VK_NONE = 0x00;
constexpr uint16_t VK_CANCEL = 0x03;// Ctrl+Break
constexpr uint16_t VK_BACK = 0x08;
constexpr uint16_t VK_TAB = 0x09;
constexpr uint16_t VK_RETURN = 0x0D;
constexpr uint16_t VK_PAUSE = 0x13;
constexpr uint16_t VK_CAPITAL = 0x14;// CapsLock
constexpr uint16_t VK_ESCAPE = 0x1B;
constexpr uint16_t VK_SPACE = 0x20;
constexpr uint16_t VK_PRIOR = 0x21;// PageUp
constexpr uint16_t VK_NEXT = 0x22; // PageDown
constexpr uint16_t VK_END = 0x23;
constexpr uint16_t VK_HOME = 0x24;
constexpr uint16_t VK_LEFT = 0x25;
constexpr uint16_t VK_UP = 0x26;
constexpr uint16_t VK_RIGHT = 0x27;
constexpr uint16_t VK_DOWN = 0x28;
constexpr uint16_t VK_SNAPSHOT = 0x2C;// PrtScn
constexpr uint16_t VK_INSERT = 0x2D;
constexpr uint16_t VK_DELETE = 0x2E;
constexpr uint16_t VK_0 = 0x30;// VK_0..VK_9 == '0'..'9'
constexpr uint16_t VK_A = 0x41;// VK_A..VK_Z == 'A'..'Z'
constexpr uint16_t VK_LWIN = 0x5B;
constexpr uint16_t VK_RWIN = 0x5C;
constexpr uint16_t VK_APPS = 0x5D;
constexpr uint16_t VK_NUMPAD0 = 0x60;// VK_NUMPAD0..VK_NUMPAD9 contiguous
constexpr uint16_t VK_MULTIPLY = 0x6A;
constexpr uint16_t VK_ADD = 0x6B;
constexpr uint16_t VK_SUBTRACT = 0x6D;
constexpr uint16_t VK_DECIMAL = 0x6E;
constexpr uint16_t VK_DIVIDE = 0x6F;
constexpr uint16_t VK_F1 = 0x70;// VK_F1..VK_F12 contiguous
constexpr uint16_t VK_NUMLOCK = 0x90;
constexpr uint16_t VK_SCROLL = 0x91;
constexpr uint16_t VK_LSHIFT = 0xA0;
constexpr uint16_t VK_RSHIFT = 0xA1;
constexpr uint16_t VK_LCONTROL = 0xA2;
constexpr uint16_t VK_RCONTROL = 0xA3;
constexpr uint16_t VK_LMENU = 0xA4;
constexpr uint16_t VK_RMENU = 0xA5;
constexpr uint16_t VK_OEM_1 = 0xBA;     // ;:
constexpr uint16_t VK_OEM_PLUS = 0xBB;  // =+
constexpr uint16_t VK_OEM_COMMA = 0xBC; // ,<
constexpr uint16_t VK_OEM_MINUS = 0xBD; // -_
constexpr uint16_t VK_OEM_PERIOD = 0xBE;// .>
constexpr uint16_t VK_OEM_2 = 0xBF;     // /?
constexpr uint16_t VK_OEM_3 = 0xC0;     // `~
constexpr uint16_t VK_OEM_4 = 0xDB;     // [{
constexpr uint16_t VK_OEM_5 = 0xDC;     // \|
constexpr uint16_t VK_OEM_6 = 0xDD;     // ]}
constexpr uint16_t VK_OEM_7 = 0xDE;     // '"
}// namespace vk

namespace detail {

constexpr kstd::array<uint16_t, 256> BuildUsageToVk() noexcept {
  using namespace vk;
  using namespace usage_kbd;
  kstd::array<uint16_t, 256> t{};
  for (auto &v: t.elements) v = VK_NONE;

  for (int i = 0; i < 26; ++i) t[A + i] = static_cast<uint16_t>(VK_A + i);
  for (int i = 0; i < 9; ++i) t[Num1 + i] = static_cast<uint16_t>(VK_0 + 1 + i);
  t[Num0] = VK_0;

  t[Enter] = VK_RETURN;
  t[Escape] = VK_ESCAPE;
  t[Backspace] = VK_BACK;
  t[Tab] = VK_TAB;
  t[Space] = VK_SPACE;
  t[Minus] = VK_OEM_MINUS;
  t[Equal] = VK_OEM_PLUS;
  t[LeftBracket] = VK_OEM_4;
  t[RightBracket] = VK_OEM_6;
  t[Backslash] = VK_OEM_5;
  t[Semicolon] = VK_OEM_1;
  t[Quote] = VK_OEM_7;
  t[Grave] = VK_OEM_3;
  t[Comma] = VK_OEM_COMMA;
  t[Period] = VK_OEM_PERIOD;
  t[Slash] = VK_OEM_2;
  t[CapsLock] = VK_CAPITAL;

  for (int i = 0; i < 12; ++i) t[F1 + i] = static_cast<uint16_t>(VK_F1 + i);

  t[PrintScreen] = VK_SNAPSHOT;
  t[ScrollLock] = VK_SCROLL;
  t[Pause] = VK_PAUSE;
  t[Insert] = VK_INSERT;
  t[Home] = VK_HOME;
  t[PageUp] = VK_PRIOR;
  t[Delete] = VK_DELETE;
  t[End] = VK_END;
  t[PageDown] = VK_NEXT;
  t[Right] = VK_RIGHT;
  t[Left] = VK_LEFT;
  t[Down] = VK_DOWN;
  t[Up] = VK_UP;

  t[NumLock] = VK_NUMLOCK;
  t[KpDivide] = VK_DIVIDE;
  t[KpMultiply] = VK_MULTIPLY;
  t[KpMinus] = VK_SUBTRACT;
  t[KpPlus] = VK_ADD;
  t[KpEnter] = VK_RETURN;
  for (int i = 0; i < 9; ++i) t[Kp1 + i] = static_cast<uint16_t>(VK_NUMPAD0 + 1 + i);
  t[Kp0] = VK_NUMPAD0;
  t[KpPeriod] = VK_DECIMAL;
  t[Application] = VK_APPS;
  t[Cancel] = VK_CANCEL;

  t[LeftControl] = VK_LCONTROL;
  t[LeftShift] = VK_LSHIFT;
  t[LeftAlt] = VK_LMENU;
  t[LeftGui] = VK_LWIN;
  t[RightControl] = VK_RCONTROL;
  t[RightShift] = VK_RSHIFT;
  t[RightAlt] = VK_RMENU;
  t[RightGui] = VK_RWIN;

  return t;
}

constexpr kstd::array<char, 256> BuildAscii(bool shifted) noexcept {
  using namespace usage_kbd;
  kstd::array<char, 256> t{};
  for (auto &v: t.elements) v = 0;

  constexpr char digitsUnshifted[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
  constexpr char digitsShifted[10] = {'!', '@', '#', '$', '%', '^', '&', '*', '(', ')'};
  for (int i = 0; i < 9; ++i) t[Num1 + i] = shifted ? digitsShifted[i] : digitsUnshifted[i];
  t[Num0] = shifted ? digitsShifted[9] : digitsUnshifted[9];

  t[Minus] = shifted ? '_' : '-';
  t[Equal] = shifted ? '+' : '=';
  t[LeftBracket] = shifted ? '{' : '[';
  t[RightBracket] = shifted ? '}' : ']';
  t[Backslash] = shifted ? '|' : '\\';
  t[Semicolon] = shifted ? ':' : ';';
  t[Quote] = shifted ? '"' : '\'';
  t[Grave] = shifted ? '~' : '`';
  t[Comma] = shifted ? '<' : ',';
  t[Period] = shifted ? '>' : '.';
  t[Slash] = shifted ? '?' : '/';
  t[Space] = ' ';

  return t;
}

}// namespace detail

// Usage -> VK_*, indexed by the Keyboard/Keypad usage ID.
constexpr kstd::array<uint16_t, 256> kUsageToVk = detail::BuildUsageToVk();

// Usage -> printable US-ASCII for the main-keyboard punctuation, digit row
// and space. Letters come from VK_A.. plus Shift/CapsLock, and the keypad's
// dual digit/navigation keys from NumLock -- both derived in hid_parser.cpp.
constexpr kstd::array<char, 256> kUsageAsciiUnshifted = detail::BuildAscii(false);
constexpr kstd::array<char, 256> kUsageAsciiShifted = detail::BuildAscii(true);

// Held-modifier bit (kernel::input::modifier_flags::*) for a modifier
// usage, or 0 for anything else. The lock keys are deliberately excluded:
// they toggle rather than hold, and are handled separately.
constexpr uint32_t HeldModifierFlag(uint16_t usage) noexcept {
  using namespace kernel::input;
  switch (usage) {
    case usage_kbd::LeftShift: return modifier_flags::LShift;
    case usage_kbd::RightShift: return modifier_flags::RShift;
    case usage_kbd::LeftControl: return modifier_flags::LControl;
    case usage_kbd::RightControl: return modifier_flags::RControl;
    case usage_kbd::LeftAlt: return modifier_flags::LAlt;
    case usage_kbd::RightAlt: return modifier_flags::RAlt;
    default: return 0;
  }
}

// Toggled-lock bit for CapsLock/NumLock/ScrollLock, or 0.
constexpr uint32_t LockModifierFlag(uint16_t usage) noexcept {
  using namespace kernel::input;
  switch (usage) {
    case usage_kbd::CapsLock: return modifier_flags::CapsLock;
    case usage_kbd::NumLock: return modifier_flags::NumLock;
    case usage_kbd::ScrollLock: return modifier_flags::ScrollLock;
    default: return 0;
  }
}

}// namespace kernel::hid
