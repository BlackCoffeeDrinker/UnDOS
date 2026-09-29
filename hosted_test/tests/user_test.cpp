#include <catch2/catch_test_macros.hpp>

// HAL-free window manager helpers from the kernel source tree.
#include "../../kernel/src/user/msgring.hpp"
#include "../../kernel/src/user/region.hpp"
#include "../../kernel/src/user/user.hpp"
#include <kernel/object_manager.hpp>

using kernel::user::MessageRing;
using kernel::user::QueuedMessage;
using kernel::user::URect;
using kernel::user::URegion;
using kernel::user::HitTest;
using kernel::user::KWindowObject;
using kernel::user::hit_test_locked;
using kernel::user::client_to_screen_locked;
using kernel::user::screen_to_client_locked;

namespace {
constexpr uint32_t kMove = 0x200;
using Ring = MessageRing<4, kMove>;

uint64_t area(const URegion &r) {
  uint64_t a = 0;
  for (uint32_t i = 0; i < r.count; ++i) {
    a += static_cast<uint64_t>(r.rects[i].right - r.rects[i].left) *
         static_cast<uint64_t>(r.rects[i].bottom - r.rects[i].top);
  }
  return a;
}
} // namespace

TEST_CASE("URegion subtract leaves the uncovered area", "[user]") {
  URegion r;
  r.Set({0, 0, 100, 100});
  r.Subtract({25, 25, 75, 75});
  REQUIRE(r.count == 4);
  REQUIRE(area(r) == 100 * 100 - 50 * 50);
  for (uint32_t i = 0; i < r.count; ++i) {
    REQUIRE(r.rects[i].Intersect({25, 25, 75, 75}).Empty());
  }
}

TEST_CASE("URegion subtract of a covering rect empties it", "[user]") {
  URegion r;
  r.Set({10, 10, 20, 20});
  r.Subtract({0, 0, 50, 50});
  REQUIRE(r.Empty());
}

TEST_CASE("URegion intersect clips to the paint rect", "[user]") {
  URegion r;
  r.Set({0, 0, 100, 100});
  r.Subtract({50, 0, 100, 100});
  r.Intersect({40, 40, 60, 60});
  REQUIRE(r.count == 1);
  REQUIRE(r.rects[0] == URect{40, 40, 50, 60});
}

TEST_CASE("MessageRing reports overflow", "[user]") {
  Ring ring;
  for (uint32_t i = 0; i < 4; ++i) REQUIRE(ring.Push({1, 0x400 + i}));
  REQUIRE_FALSE(ring.Push({1, 0x500}));
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.message == 0x400);
}

TEST_CASE("MessageRing coalesces consecutive mouse moves per window", "[user]") {
  Ring ring;
  REQUIRE(ring.Push({1, kMove, 0, 1}));
  REQUIRE(ring.Push({1, kMove, 0, 2}));
  REQUIRE(ring.count == 1);
  REQUIRE(ring.Push({2, kMove, 0, 3}));
  REQUIRE(ring.count == 2);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.lParam == 2);
}

TEST_CASE("MessageRing drops a destroyed window's messages", "[user]") {
  Ring ring;
  ring.Push({1, 0x400});
  ring.Push({2, 0x401});
  ring.Push({1, 0x402});
  ring.RemoveFor(1);
  REQUIRE(ring.count == 1);
  QueuedMessage m;
  REQUIRE(ring.Pop(m));
  REQUIRE(m.hwnd == 2);
}

TEST_CASE("Window hit testing", "[user]") {
  KWindowObject w;
  w.rect = {100, 100, 300, 200}; // Width 200, Height 100
  w.style = kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder;

  // Border is 3px, Caption is 18px.
  // Client area starts at (103, 100 + 3 + 18) = (103, 121)

  REQUIRE(hit_test_locked(w, 50, 50) == HitTest::Nowhere);
  REQUIRE(hit_test_locked(w, 101, 101) == HitTest::TopLeft);
  REQUIRE(hit_test_locked(w, 150, 101) == HitTest::Top);
  REQUIRE(hit_test_locked(w, 150, 110) == HitTest::Caption);
  REQUIRE(hit_test_locked(w, 150, 150) == HitTest::Client);
  REQUIRE(hit_test_locked(w, 300 - 5, 110) == HitTest::Close);
}

TEST_CASE("Coordinate mapping", "[user]") {
  KWindowObject w;
  w.rect = {100, 100, 300, 200};
  w.style = kernel::user::kStyleVisible | kernel::user::kStyleCaption | kernel::user::kStyleBorder;

  URect client = {0, 0, 10, 10};
  URect screen = client_to_screen_locked(w, client);
  REQUIRE(screen.left == 100 + 3);
  REQUIRE(screen.top == 100 + 3 + 18);

  URect back = screen_to_client_locked(w, screen);
  REQUIRE(back == client);
}
