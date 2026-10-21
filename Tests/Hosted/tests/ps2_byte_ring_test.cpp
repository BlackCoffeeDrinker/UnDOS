
#include "ps2_byte_ring.hpp"

#include <catch2/catch_test_macros.hpp>

using ps2::ByteRing;

TEST_CASE("ByteRing preserves order", "[ps2_byte_ring]") {
  ByteRing ring;
  REQUIRE(ring.Empty());

  for (uint8_t i = 0; i < 10; ++i) REQUIRE(ring.Push(i));
  REQUIRE_FALSE(ring.Empty());

  uint8_t out = 0;
  for (uint8_t i = 0; i < 10; ++i) {
    REQUIRE(ring.Pop(out));
    REQUIRE(out == i);
  }
  REQUIRE_FALSE(ring.Pop(out));
  REQUIRE(ring.Empty());
}

TEST_CASE("ByteRing drops on full rather than overwriting unread data", "[ps2_byte_ring]") {
  ByteRing ring;

  // One slot is always kept free to distinguish full from empty.
  size_t accepted = 0;
  for (size_t i = 0; i < ByteRing::kCapacity + 16; ++i) {
    if (ring.Push(static_cast<uint8_t>(i))) ++accepted;
  }
  REQUIRE(accepted == ByteRing::kCapacity - 1);

  // The bytes that were accepted must still come back in order; the drops
  // were at the tail of the burst, never an eviction of buffered data.
  uint8_t out = 0;
  for (size_t i = 0; i < accepted; ++i) {
    REQUIRE(ring.Pop(out));
    REQUIRE(out == static_cast<uint8_t>(i));
  }
  REQUIRE_FALSE(ring.Pop(out));
}

TEST_CASE("ByteRing preserves order across a wrap-around", "[ps2_byte_ring]") {
  ByteRing ring;
  uint8_t out = 0;

  // Drive head/tail well past the capacity boundary several times.
  for (size_t round = 0; round < 5; ++round) {
    for (size_t i = 0; i < 200; ++i) {
      REQUIRE(ring.Push(static_cast<uint8_t>(i)));
    }
    for (size_t i = 0; i < 200; ++i) {
      REQUIRE(ring.Pop(out));
      REQUIRE(out == static_cast<uint8_t>(i));
    }
    REQUIRE(ring.Empty());
  }
}
