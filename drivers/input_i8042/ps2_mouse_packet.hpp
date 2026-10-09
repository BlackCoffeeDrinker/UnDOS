#pragma once

#include <stdint.h>

#include <kernel/input.hpp>

// Pure, header-only PS/2 aux byte-stream -> MousePacket state machine. No
// kernel calls: the i8042 pump thread (drivers/input_i8042/ps2_pump.cpp)
// feeds it raw bytes drained from the aux byte ring and packs the result
// into a synthetic HID mouse report. Freestanding, so it compiles unchanged
// into KernelTests (see hosted_test/tests/mouse_packet_test.cpp), exactly
// like ps2_key_translate.hpp / ps2_scancode_tables.hpp.
//
// `MousePacket` and `mouse_buttons::*` are the hardware-agnostic contract
// and come from <kernel/input.hpp>; only the wire framing lives here.
namespace ps2 {

using kernel::input::MousePacket;
namespace mouse_buttons = kernel::input::mouse_buttons;

// Mouse packet framing negotiated with the aux device (see
// I8042_NegotiateMouseExtensions in i8042.cpp). Standard3Byte is always the
// safe default; upgraded only after the device confirms a recognised device
// ID for the corresponding knock sequence.
enum class MousePacketMode : uint8_t {
  Standard3Byte,
  IntelliMouse4Byte,
  IntelliMouseExplorer4Byte,
};

class MousePacketDecoder {
  public:
  // Switches packet framing. Any partially-received packet is discarded
  // rather than misparsed against the new length.
  void SetMode(MousePacketMode mode) noexcept {
    m_mode = mode;
    m_byteIndex = 0;
  }

  [[nodiscard]] MousePacketMode GetMode() const noexcept { return m_mode; }

  // Feeds one raw aux byte. Returns true and fills `out` once a full
  // packet has been validated and decoded; returns false (leaving `out`
  // untouched) when more bytes are needed, the packet was rejected
  // (overflow), or a desync byte was discarded.
  bool Feed(uint8_t byte, MousePacket &out) noexcept {
    const int packetLength = (m_mode == MousePacketMode::Standard3Byte) ? 3 : 4;

    if (m_byteIndex == 0) {
      // Byte 0 must always have bit 3 set; anything else means the stream
      // is desynchronised (e.g. we started listening mid-packet). Discard
      // the byte and keep waiting for a valid byte 0 -- this is the whole
      // of the decoder's desync recovery.
      if ((byte & 0x08) == 0) {
        return false;
      }
      m_bytes[0] = byte;
      m_byteIndex = 1;
      return false;
    }

    m_bytes[m_byteIndex++] = byte;
    if (m_byteIndex < packetLength) {
      return false;
    }
    m_byteIndex = 0;

    const uint8_t b0 = m_bytes[0];
    if (b0 & 0xC0) {
      // Bit 6 = X overflow, bit 7 = Y overflow: the whole packet is
      // discarded, never partially trusted.
      return false;
    }

    // 9-bit sign extension: the sign bit for X is byte0 bit4, for Y byte0
    // bit5; shifting it up to bit8 and subtracting reproduces two's
    // complement for the wider type.
    const auto dx = static_cast<int16_t>(
        static_cast<int16_t>(m_bytes[1]) - static_cast<int16_t>((static_cast<uint16_t>(b0) << 4) & 0x100));
    const auto rawDy = static_cast<int16_t>(
        static_cast<int16_t>(m_bytes[2]) - static_cast<int16_t>((static_cast<uint16_t>(b0) << 3) & 0x100));

    out.dx = dx;
    out.dy = static_cast<int16_t>(-rawDy);// raw PS/2 Y is inverted vs. screen space
    out.buttons = b0 & 0x07;              // Left/Right/Middle
    out.wheel = 0;
    out.isAbsolute = false;

    if (m_mode == MousePacketMode::IntelliMouse4Byte) {
      // Full signed byte; devices only ever use the -8..7 range, but the
      // wire format is a plain int8_t.
      out.wheel = static_cast<int8_t>(m_bytes[3]);
    } else if (m_mode == MousePacketMode::IntelliMouseExplorer4Byte) {
      // Low nibble is a signed 4-bit wheel delta; bits 4/5 are buttons 4/5.
      const uint8_t nibble = m_bytes[3] & 0x0F;
      out.wheel = static_cast<int8_t>((nibble ^ 0x08) - 0x08);// sign-extend 4 bits
      if (m_bytes[3] & 0x10) out.buttons |= mouse_buttons::Button4;
      if (m_bytes[3] & 0x20) out.buttons |= mouse_buttons::Button5;
    }

    return true;
  }

  private:
  MousePacketMode m_mode = MousePacketMode::Standard3Byte;
  uint8_t m_bytes[4] = {0, 0, 0, 0};
  int m_byteIndex = 0;
};

}// namespace ps2
