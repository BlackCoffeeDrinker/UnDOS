
#include "ps2_mouse_packet.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ps2;
using namespace kernel::input;

TEST_CASE("MousePacketDecoder canonical right/down packet", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  // byte0: bit3 set, no overflow, no sign bits, left button held.
  REQUIRE_FALSE(decoder.Feed(0b0000'1001, packet));
  REQUIRE_FALSE(decoder.Feed(10, packet));// dx = +10
  REQUIRE(decoder.Feed(5, packet));       // dy raw = +5 -> screen dy = -5

  REQUIRE(packet.dx == 10);
  REQUIRE(packet.dy == -5);
  REQUIRE((packet.buttons & mouse_buttons::Left) != 0);
  REQUIRE((packet.buttons & mouse_buttons::Right) == 0);
  REQUIRE(packet.wheel == 0);
}

TEST_CASE("MousePacketDecoder negative deltas", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  // X sign bit (bit4) and Y sign bit (bit5) both set.
  REQUIRE_FALSE(decoder.Feed(0b0011'1000, packet));
  REQUIRE_FALSE(decoder.Feed(static_cast<uint8_t>(-10), packet));// dx = -10
  REQUIRE(decoder.Feed(static_cast<uint8_t>(-5), packet));       // dy raw = -5 -> screen dy = +5

  REQUIRE(packet.dx == -10);
  REQUIRE(packet.dy == 5);
  REQUIRE(packet.buttons == 0);
}

TEST_CASE("MousePacketDecoder sign-extension extremes", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  // dx = -256: sign bit set, byte1 = 0x00.
  REQUIRE_FALSE(decoder.Feed(0b0001'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0x00, packet));
  REQUIRE(decoder.Feed(0x00, packet));
  REQUIRE(packet.dx == -256);

  // dx = +255: sign bit clear, byte1 = 0xFF.
  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0xFF, packet));
  REQUIRE(decoder.Feed(0x00, packet));
  REQUIRE(packet.dx == 255);
}

TEST_CASE("MousePacketDecoder overflow bit discards the packet", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  // bit6 (X overflow) set alongside the mandatory bit3.
  REQUIRE_FALSE(decoder.Feed(0b0100'1000, packet));
  REQUIRE_FALSE(decoder.Feed(10, packet));
  REQUIRE_FALSE(decoder.Feed(10, packet));// full packet received, but overflow -> no packet emitted
}

TEST_CASE("MousePacketDecoder resynchronises after a mid-packet start", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  // Simulate the ring starting mid-stream: first two bytes look like a
  // valid byte0 (bit3 set) but are actually stray data bytes -- fed as
  // byte0 candidates, they must not desync the next genuine packet.
  REQUIRE_FALSE(decoder.Feed(0x40, packet));// bit3 clear: discarded, still waiting for byte0
  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(3, packet));
  REQUIRE(decoder.Feed(4, packet));
  REQUIRE(packet.dx == 3);
  REQUIRE(packet.dy == -4);
}

TEST_CASE("MousePacketDecoder a pure-garbage stream never emits a packet", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  for (const uint8_t b: {static_cast<uint8_t>(0x00), static_cast<uint8_t>(0x01), static_cast<uint8_t>(0x02),
                   static_cast<uint8_t>(0x03), static_cast<uint8_t>(0x04), static_cast<uint8_t>(0x05),
                   static_cast<uint8_t>(0x06), static_cast<uint8_t>(0x07), static_cast<uint8_t>(0x70),
                   static_cast<uint8_t>(0xF0)}) {
    REQUIRE_FALSE(decoder.Feed(b, packet));
  }
}

TEST_CASE("MousePacketDecoder button-only packet with zero motion", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  REQUIRE_FALSE(decoder.Feed(0b0000'1011, packet));// Left + Right held
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE(decoder.Feed(0, packet));

  REQUIRE(packet.dx == 0);
  REQUIRE(packet.dy == 0);
  REQUIRE((packet.buttons & mouse_buttons::Left) != 0);
  REQUIRE((packet.buttons & mouse_buttons::Right) != 0);
  REQUIRE((packet.buttons & mouse_buttons::Middle) == 0);
}

TEST_CASE("MousePacketDecoder IntelliMouse4Byte positive and negative wheel", "[mouse_packet]") {
  MousePacketDecoder decoder;
  decoder.SetMode(MousePacketMode::IntelliMouse4Byte);
  MousePacket packet{};

  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE(decoder.Feed(3, packet));// scroll up 3 detents
  REQUIRE(packet.wheel == 3);

  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE(decoder.Feed(static_cast<uint8_t>(-3), packet));// scroll down 3 detents
  REQUIRE(packet.wheel == -3);
}

TEST_CASE("MousePacketDecoder IntelliMouseExplorer4Byte button 4/5 plus wheel", "[mouse_packet]") {
  MousePacketDecoder decoder;
  decoder.SetMode(MousePacketMode::IntelliMouseExplorer4Byte);
  MousePacket packet{};

  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  // byte3: bit4 (Button4) set, low nibble = 1 (wheel +1).
  REQUIRE(decoder.Feed(0b0001'0001, packet));
  REQUIRE(packet.wheel == 1);
  REQUIRE((packet.buttons & mouse_buttons::Button4) != 0);
  REQUIRE((packet.buttons & mouse_buttons::Button5) == 0);

  // byte3: bit5 (Button5) set, low nibble = 0xF (wheel -1, sign-extended).
  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE(decoder.Feed(0b0010'1111, packet));
  REQUIRE(packet.wheel == -1);
  REQUIRE((packet.buttons & mouse_buttons::Button5) != 0);
  REQUIRE((packet.buttons & mouse_buttons::Button4) == 0);
}

TEST_CASE("MousePacketDecoder SetMode mid-stream discards a partial packet", "[mouse_packet]") {
  MousePacketDecoder decoder;
  MousePacket packet{};

  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));// byte0 of a 3-byte packet consumed
  decoder.SetMode(MousePacketMode::IntelliMouse4Byte);// discards the pending byte0

  // Feed a full valid 4-byte packet from scratch; if the partial state had
  // survived, this would misparse against the wrong byte offsets.
  REQUIRE_FALSE(decoder.Feed(0b0000'1000, packet));
  REQUIRE_FALSE(decoder.Feed(1, packet));
  REQUIRE_FALSE(decoder.Feed(0, packet));
  REQUIRE(decoder.Feed(0, packet));
  REQUIRE(packet.dx == 1);
  REQUIRE(packet.dy == 0);
}
