#pragma once

#include <array.hpp>
#include <stdint.h>

// PS/2 scancode set 1 -> virtual-key (VK_*) tables, plus the ASCII tables
// used by key_translate.hpp to derive KeyboardInputEvent::character.
// Header-only, freestanding, no kernel dependencies, so it compiles
// unchanged into KernelTests (see hosted_test/tests/input_scancode_test.cpp),
// exactly like drivers/fs_fat/fat_short_name.hpp.
namespace scancode_tables {

// Win32-compatible virtual-key codes for the subset of keys this driver
// decodes. VK_NONE (0) means "no virtual key for this scancode".
constexpr uint16_t VK_NONE = 0x00;
constexpr uint16_t VK_CANCEL = 0x03;  // Ctrl+Break
constexpr uint16_t VK_BACK = 0x08;
constexpr uint16_t VK_TAB = 0x09;
constexpr uint16_t VK_RETURN = 0x0D;
constexpr uint16_t VK_SHIFT = 0x10;
constexpr uint16_t VK_CONTROL = 0x11;
constexpr uint16_t VK_MENU = 0x12;   // Alt
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

namespace detail {

constexpr kstd::array<uint16_t, 128> BuildBaseTable() noexcept {
  kstd::array<uint16_t, 128> t{};
  for (auto &v: t.elements) v = VK_NONE;

  t[0x01] = VK_ESCAPE;
  t[0x02] = static_cast<uint16_t>(VK_0 + 1);
  t[0x03] = static_cast<uint16_t>(VK_0 + 2);
  t[0x04] = static_cast<uint16_t>(VK_0 + 3);
  t[0x05] = static_cast<uint16_t>(VK_0 + 4);
  t[0x06] = static_cast<uint16_t>(VK_0 + 5);
  t[0x07] = static_cast<uint16_t>(VK_0 + 6);
  t[0x08] = static_cast<uint16_t>(VK_0 + 7);
  t[0x09] = static_cast<uint16_t>(VK_0 + 8);
  t[0x0A] = static_cast<uint16_t>(VK_0 + 9);
  t[0x0B] = static_cast<uint16_t>(VK_0 + 0);
  t[0x0C] = VK_OEM_MINUS;
  t[0x0D] = VK_OEM_PLUS;
  t[0x0E] = VK_BACK;
  t[0x0F] = VK_TAB;
  t[0x10] = static_cast<uint16_t>(VK_A + ('Q' - 'A'));
  t[0x11] = static_cast<uint16_t>(VK_A + ('W' - 'A'));
  t[0x12] = static_cast<uint16_t>(VK_A + ('E' - 'A'));
  t[0x13] = static_cast<uint16_t>(VK_A + ('R' - 'A'));
  t[0x14] = static_cast<uint16_t>(VK_A + ('T' - 'A'));
  t[0x15] = static_cast<uint16_t>(VK_A + ('Y' - 'A'));
  t[0x16] = static_cast<uint16_t>(VK_A + ('U' - 'A'));
  t[0x17] = static_cast<uint16_t>(VK_A + ('I' - 'A'));
  t[0x18] = static_cast<uint16_t>(VK_A + ('O' - 'A'));
  t[0x19] = static_cast<uint16_t>(VK_A + ('P' - 'A'));
  t[0x1A] = VK_OEM_4;
  t[0x1B] = VK_OEM_6;
  t[0x1C] = VK_RETURN;
  t[0x1D] = VK_LCONTROL;
  t[0x1E] = static_cast<uint16_t>(VK_A + ('A' - 'A'));
  t[0x1F] = static_cast<uint16_t>(VK_A + ('S' - 'A'));
  t[0x20] = static_cast<uint16_t>(VK_A + ('D' - 'A'));
  t[0x21] = static_cast<uint16_t>(VK_A + ('F' - 'A'));
  t[0x22] = static_cast<uint16_t>(VK_A + ('G' - 'A'));
  t[0x23] = static_cast<uint16_t>(VK_A + ('H' - 'A'));
  t[0x24] = static_cast<uint16_t>(VK_A + ('J' - 'A'));
  t[0x25] = static_cast<uint16_t>(VK_A + ('K' - 'A'));
  t[0x26] = static_cast<uint16_t>(VK_A + ('L' - 'A'));
  t[0x27] = VK_OEM_1;
  t[0x28] = VK_OEM_7;
  t[0x29] = VK_OEM_3;
  t[0x2A] = VK_LSHIFT;
  t[0x2B] = VK_OEM_5;
  t[0x2C] = static_cast<uint16_t>(VK_A + ('Z' - 'A'));
  t[0x2D] = static_cast<uint16_t>(VK_A + ('X' - 'A'));
  t[0x2E] = static_cast<uint16_t>(VK_A + ('C' - 'A'));
  t[0x2F] = static_cast<uint16_t>(VK_A + ('V' - 'A'));
  t[0x30] = static_cast<uint16_t>(VK_A + ('B' - 'A'));
  t[0x31] = static_cast<uint16_t>(VK_A + ('N' - 'A'));
  t[0x32] = static_cast<uint16_t>(VK_A + ('M' - 'A'));
  t[0x33] = VK_OEM_COMMA;
  t[0x34] = VK_OEM_PERIOD;
  t[0x35] = VK_OEM_2;
  t[0x36] = VK_RSHIFT;
  t[0x37] = VK_MULTIPLY;// KP *
  t[0x38] = VK_LMENU;
  t[0x39] = VK_SPACE;
  t[0x3A] = VK_CAPITAL;
  for (int i = 0; i < 10; ++i) t[0x3B + i] = static_cast<uint16_t>(VK_F1 + i);// F1..F10
  t[0x45] = VK_NUMLOCK;
  t[0x46] = VK_SCROLL;
  t[0x47] = static_cast<uint16_t>(VK_NUMPAD0 + 7);// KP 7 / Home
  t[0x48] = static_cast<uint16_t>(VK_NUMPAD0 + 8);// KP 8 / Up
  t[0x49] = static_cast<uint16_t>(VK_NUMPAD0 + 9);// KP 9 / PageUp
  t[0x4A] = VK_SUBTRACT;                          // KP -
  t[0x4B] = static_cast<uint16_t>(VK_NUMPAD0 + 4);// KP 4 / Left
  t[0x4C] = static_cast<uint16_t>(VK_NUMPAD0 + 5);// KP 5
  t[0x4D] = static_cast<uint16_t>(VK_NUMPAD0 + 6);// KP 6 / Right
  t[0x4E] = VK_ADD;                                // KP +
  t[0x4F] = static_cast<uint16_t>(VK_NUMPAD0 + 1);// KP 1 / End
  t[0x50] = static_cast<uint16_t>(VK_NUMPAD0 + 2);// KP 2 / Down
  t[0x51] = static_cast<uint16_t>(VK_NUMPAD0 + 3);// KP 3 / PageDown
  t[0x52] = VK_NUMPAD0;                            // KP 0 / Insert
  t[0x53] = VK_DECIMAL;                            // KP . / Delete
  t[0x57] = static_cast<uint16_t>(VK_F1 + 10);// F11
  t[0x58] = static_cast<uint16_t>(VK_F1 + 11);// F12

  return t;
}

constexpr kstd::array<char, 128> BuildAsciiUnshifted() noexcept {
  kstd::array<char, 128> t{};
  for (auto &v: t.elements) v = 0;

  t[0x02] = '1';
  t[0x03] = '2';
  t[0x04] = '3';
  t[0x05] = '4';
  t[0x06] = '5';
  t[0x07] = '6';
  t[0x08] = '7';
  t[0x09] = '8';
  t[0x0A] = '9';
  t[0x0B] = '0';
  t[0x0C] = '-';
  t[0x0D] = '=';
  t[0x1A] = '[';
  t[0x1B] = ']';
  t[0x27] = ';';
  t[0x28] = '\'';
  t[0x29] = '`';
  t[0x2B] = '\\';
  t[0x33] = ',';
  t[0x34] = '.';
  t[0x35] = '/';
  t[0x39] = ' ';

  return t;
}

constexpr kstd::array<char, 128> BuildAsciiShifted() noexcept {
  kstd::array<char, 128> t{};
  for (auto &v: t.elements) v = 0;

  t[0x02] = '!';
  t[0x03] = '@';
  t[0x04] = '#';
  t[0x05] = '$';
  t[0x06] = '%';
  t[0x07] = '^';
  t[0x08] = '&';
  t[0x09] = '*';
  t[0x0A] = '(';
  t[0x0B] = ')';
  t[0x0C] = '_';
  t[0x0D] = '+';
  t[0x1A] = '{';
  t[0x1B] = '}';
  t[0x27] = ':';
  t[0x28] = '"';
  t[0x29] = '~';
  t[0x2B] = '|';
  t[0x33] = '<';
  t[0x34] = '>';
  t[0x35] = '?';
  t[0x39] = ' ';

  return t;
}

}// namespace detail

// Base (non-extended) scancode set 1, indexed by the scancode with the
// break bit (0x80) masked off. VK_NONE for unmapped/unused codes.
constexpr kstd::array<uint16_t, 128> kSet1BaseToVk = detail::BuildBaseTable();

// ASCII derived from the base scancode for the main-keyboard punctuation,
// digit row and space; 0 means "no printable character here" -- letters are
// derived from VK_A.. + Shift/CapsLock, and the keypad's dual digit/nav keys
// are derived from NumLock, both in key_translate.hpp.
constexpr kstd::array<char, 128> kAsciiUnshifted = detail::BuildAsciiUnshifted();
constexpr kstd::array<char, 128> kAsciiShifted = detail::BuildAsciiShifted();

// Sparse E0-prefixed extended scancode -> VK lookup (navigation cluster,
// RCtrl/RAlt, KP-Enter, KP-/, Win/Apps, PrtScn, Ctrl+Break).
constexpr uint16_t LookupExtendedVk(uint8_t scanCode) noexcept {
  switch (scanCode) {
    case 0x1C: return VK_RETURN;  // KP Enter
    case 0x1D: return VK_RCONTROL;
    case 0x35: return VK_DIVIDE;  // KP /
    case 0x37: return VK_SNAPSHOT;// PrtScn
    case 0x38: return VK_RMENU;
    case 0x46: return VK_CANCEL;// Ctrl+Break (E0 46)
    case 0x47: return VK_HOME;
    case 0x48: return VK_UP;
    case 0x49: return VK_PRIOR;
    case 0x4B: return VK_LEFT;
    case 0x4D: return VK_RIGHT;
    case 0x4F: return VK_END;
    case 0x50: return VK_DOWN;
    case 0x51: return VK_NEXT;
    case 0x52: return VK_INSERT;
    case 0x53: return VK_DELETE;
    case 0x5B: return VK_LWIN;
    case 0x5C: return VK_RWIN;
    case 0x5D: return VK_APPS;
    default: return VK_NONE;
  }
}

}// namespace scancode_tables
