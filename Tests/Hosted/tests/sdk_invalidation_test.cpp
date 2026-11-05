#include <catch2/catch_test_macros.hpp>

#include <undosgui/invalidation.h>

namespace {
struct Request {
  uint32_t hwnd{};
  Rect rect{};
  uint32_t whole{};
  uint32_t erase{};
  uint32_t result{};
} g_request;

uint32_t CaptureInvalidate(uint32_t hwnd, const Rect *rect, uint32_t whole,
                           uint32_t erase) {
  g_request = {hwnd, *rect, whole, erase, 0x1234};
  return g_request.result;
}
}  // namespace

TEST_CASE("SDK InvalidateRect passes bErase and rectangle to USER V2",
          "[sdk][invalidate]") {
  const Rect input{2, 3, 17, 19};
  const uint32_t result = udosgui_invalidate_rect_v2(
      0x8765, &input, 0, CaptureInvalidate);
  REQUIRE(result == 0x1234);
  REQUIRE(g_request.hwnd == 0x8765);
  REQUIRE(g_request.rect.left == 2);
  REQUIRE(g_request.rect.top == 3);
  REQUIRE(g_request.rect.right == 17);
  REQUIRE(g_request.rect.bottom == 19);
  REQUIRE(g_request.whole == 0);
  REQUIRE(g_request.erase == 0);

  (void)udosgui_invalidate_rect_v2(0x8765, nullptr, 1,
                                   CaptureInvalidate);
  REQUIRE(g_request.rect.left == 0);
  REQUIRE(g_request.rect.top == 0);
  REQUIRE(g_request.rect.right == 0);
  REQUIRE(g_request.rect.bottom == 0);
  REQUIRE(g_request.whole == 1);
  REQUIRE(g_request.erase == 1);
}

TEST_CASE("SDK gui invalidate requests erase for the whole window",
          "[sdk][invalidate]") {
  REQUIRE(udosgui_invalidate_whole_v2(0x4321, CaptureInvalidate) == 0x1234);
  REQUIRE(g_request.hwnd == 0x4321);
  REQUIRE(g_request.whole == 1);
  REQUIRE(g_request.erase == 1);
}
