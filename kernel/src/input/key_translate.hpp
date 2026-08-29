#pragma once

#include <kernel/input.hpp>

#include "scancode_tables.hpp"

// Pure, header-only scancode-set-1 -> KeyboardInputEvent state machine. No
// kernel calls: the input manager (kernel/src/input/input_manager.cpp) feeds
// it raw bytes drained from the keyboard byte ring and stamps the resulting
// event's timestamp with KE_TIME_GetSystemTime() before queueing it.
// Freestanding, so it compiles unchanged into KernelTests (see
// hosted_test/tests/input_scancode_test.cpp).
namespace kernel::input {

class KeyTranslator {
  public:
  // Feeds one raw scancode-set-1 byte. Returns true and fills `out` when a
  // full key transition has been decoded; returns false (leaving `out`
  // untouched) when more bytes are needed or the byte was consumed by a
  // prefix/protocol sequence.
  bool Feed(uint8_t byte, KeyboardInputEvent &out) noexcept {
    // Keyboard protocol/ack bytes that can appear on the data stream
    // outside of an explicit reset handshake (e.g. a stray re-ACK); never a
    // scancode, so they must not desync the prefix state machine below.
    if (byte == 0xFA || byte == 0xFE || byte == 0xEE || byte == 0x00 || byte == 0xFF) {
      return false;
    }

    if (m_pauseBytesLeft > 0) {
      return FeedPauseByte(out);
    }

    if (byte == 0xE1) {
      // Pause/Break sends a fixed 6-byte sequence with no separate break
      // code: E1 1D 45 E1 9D C5. Swallow all 6 bytes (this one plus the 5
      // that follow) and emit a single VK_PAUSE make event once complete.
      m_pauseBytesLeft = 5;
      return false;
    }

    if (byte == 0xE0) {
      // One-shot prefix; a second E0 before a real scancode simply keeps it
      // pending rather than corrupting the state machine.
      m_extendedPending = true;
      return false;
    }

    const bool extended = m_extendedPending;
    m_extendedPending = false;

    const bool isBreak = (byte & 0x80) != 0;
    const uint8_t scanCode = static_cast<uint8_t>(byte & 0x7F);

    const uint16_t vk = extended ? scancode_tables::LookupExtendedVk(scanCode)
                                  : scancode_tables::kSet1BaseToVk[scanCode];
    if (vk == scancode_tables::VK_NONE) {
      return false;
    }

    UpdateModifiers(vk, isBreak);

    out = KeyboardInputEvent{};
    out.virtualKey = vk;
    out.scanCode = scanCode;
    out.keyFlags = 0;
    if (isBreak) out.keyFlags |= key_flags::Break;
    if (extended) out.keyFlags |= key_flags::Extended;

    const bool wasDown = TestDownBit(scanCode);
    if (isBreak) {
      SetDownBit(scanCode, false);
    } else {
      if (wasDown) out.keyFlags |= key_flags::Repeat;
      SetDownBit(scanCode, true);
    }

    out.character = DeriveCharacter(scanCode, extended, vk, isBreak);
    out.modifiers = m_modifiers;

    return true;
  }

  [[nodiscard]] uint32_t GetModifiers() const noexcept { return m_modifiers; }

  private:
  bool FeedPauseByte(KeyboardInputEvent &out) noexcept {
    // The exact byte values (1D, 45, E1, 9D, C5) are fixed by the protocol
    // and not individually validated -- any 5 bytes following the leading
    // E1 complete the swallow, which is sufficient to never desync on a
    // genuine Pause key and to always resynchronise after at most 5 bytes
    // even if the stream was corrupted.
    if (--m_pauseBytesLeft > 0) {
      return false;
    }

    out = KeyboardInputEvent{};
    out.virtualKey = scancode_tables::VK_PAUSE;
    out.scanCode = 0;
    out.keyFlags = 0;
    out.character = 0;
    out.modifiers = m_modifiers;
    return true;
  }

  void UpdateModifiers(uint16_t vk, bool isBreak) noexcept {
    using namespace scancode_tables;
    uint32_t bit = 0;
    switch (vk) {
      case VK_LSHIFT: bit = modifier_flags::LShift; break;
      case VK_RSHIFT: bit = modifier_flags::RShift; break;
      case VK_LCONTROL: bit = modifier_flags::LControl; break;
      case VK_RCONTROL: bit = modifier_flags::RControl; break;
      case VK_LMENU: bit = modifier_flags::LAlt; break;
      case VK_RMENU: bit = modifier_flags::RAlt; break;
      case VK_CAPITAL:
        if (!isBreak) m_modifiers ^= modifier_flags::CapsLock;// toggled on make only
        return;
      case VK_NUMLOCK:
        if (!isBreak) m_modifiers ^= modifier_flags::NumLock;
        return;
      case VK_SCROLL:
        if (!isBreak) m_modifiers ^= modifier_flags::ScrollLock;
        return;
      default:
        return;
    }
    if (isBreak) {
      m_modifiers &= ~bit;
    } else {
      m_modifiers |= bit;
    }
  }

  [[nodiscard]] char DeriveCharacter(uint8_t scanCode, bool extended, uint16_t vk, bool isBreak) const noexcept {
    using namespace scancode_tables;
    if (isBreak) return 0;// only make events carry a character

    if (vk >= VK_A && vk < VK_A + 26) {
      const bool shifted = (m_modifiers & modifier_flags::Shift) != 0;
      const bool caps = (m_modifiers & modifier_flags::CapsLock) != 0;
      const bool upper = shifted != caps;// CapsLock only flips letter case, unlike Shift
      return static_cast<char>((upper ? 'A' : 'a') + (vk - VK_A));
    }

    // Keypad dual digit/navigation keys: a character only when NumLock is
    // on and the code was NOT E0-prefixed -- the navigation-cluster form of
    // the same scancode never carries a keypad character.
    if (!extended && vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9) {
      return (m_modifiers & modifier_flags::NumLock) ? static_cast<char>('0' + (vk - VK_NUMPAD0)) : 0;
    }
    if (!extended && vk == VK_DECIMAL) {
      return (m_modifiers & modifier_flags::NumLock) ? '.' : 0;
    }
    // KP *, +, - always produce their symbol regardless of NumLock; KP / is
    // only reachable via the E0-prefixed form, ahead of the extended-cluster
    // check below.
    if (vk == VK_MULTIPLY) return '*';
    if (vk == VK_ADD) return '+';
    if (vk == VK_SUBTRACT) return '-';
    if (vk == VK_DIVIDE) return '/';

    if (extended) return 0;// navigation cluster / RCtrl / RAlt / Win / Apps: no character

    const bool shifted = (m_modifiers & modifier_flags::Shift) != 0;
    return shifted ? kAsciiShifted[scanCode] : kAsciiUnshifted[scanCode];
  }

  [[nodiscard]] bool TestDownBit(uint8_t scanCode) const noexcept {
    return (m_downBits[scanCode / 32] & (1u << (scanCode % 32))) != 0;
  }

  void SetDownBit(uint8_t scanCode, bool down) noexcept {
    const uint32_t mask = 1u << (scanCode % 32);
    if (down) {
      m_downBits[scanCode / 32] |= mask;
    } else {
      m_downBits[scanCode / 32] &= ~mask;
    }
  }

  bool m_extendedPending = false;
  int m_pauseBytesLeft = 0;
  uint32_t m_modifiers = 0;
  uint32_t m_downBits[4] = {0, 0, 0, 0};// 128-bit down-state bitmap, indexed by base scanCode (0-127)
};

}// namespace kernel::input
