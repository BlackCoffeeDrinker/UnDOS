#pragma once

#include <stdint.h>

#include "ps2_scancode_tables.hpp"

// Pure, header-only scancode-set-1 -> HID usage transition state machine.
// No kernel calls: the i8042 pump thread (drivers/input_i8042/ps2_pump.cpp)
// feeds it raw bytes drained from the keyboard byte ring, and hands each
// completed transition to the HID report packer. Modifier tracking, virtual
// keys and ASCII all live above this, in drivers/input_hid/. Freestanding,
// so it compiles unchanged into KernelTests (see
// hosted_test/tests/input_scancode_test.cpp).
namespace ps2 {

// One decoded key transition: which HID usage, and whether it went up.
struct KeyTransition {
  uint16_t usage = kernel::hid::usage_kbd::None;
  bool isBreak = false;
};

class KeyTranslator {
  public:
  // Feeds one raw scancode-set-1 byte. Returns true and fills `out` when a
  // full key transition has been decoded; returns false (leaving `out`
  // untouched) when more bytes are needed or the byte was consumed by a
  // prefix/protocol sequence.
  bool Feed(uint8_t byte, KeyTransition &out) noexcept {
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
      // that follow) and emit a single Pause make once complete.
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

    const uint16_t usage = extended ? LookupExtendedUsage(scanCode) : kSet1BaseToUsage[scanCode];
    if (usage == kernel::hid::usage_kbd::None) {
      return false;
    }

    out.usage = usage;
    out.isBreak = isBreak;
    return true;
  }

  private:
  bool FeedPauseByte(KeyTransition &out) noexcept {
    // The exact byte values (1D, 45, E1, 9D, C5) are fixed by the protocol
    // and not individually validated -- any 5 bytes following the leading
    // E1 complete the swallow, which is sufficient to never desync on a
    // genuine Pause key and to always resynchronise after at most 5 bytes
    // even if the stream was corrupted.
    if (--m_pauseBytesLeft > 0) {
      return false;
    }

    out.usage = kernel::hid::usage_kbd::Pause;
    out.isBreak = false;
    return true;
  }

  bool m_extendedPending = false;
  int m_pauseBytesLeft = 0;
};

}// namespace ps2
