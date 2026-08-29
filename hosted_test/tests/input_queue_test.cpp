
#include "../../kernel/src/input/event_ring.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace kernel::input;

namespace {

AnyInputEvent MakeKeyEvent(uint16_t vk) {
  AnyInputEvent event;
  event.keyboard = KeyboardInputEvent{};
  event.keyboard.virtualKey = vk;
  return event;
}

AnyInputEvent MakeMotionMouseEvent(int32_t dx, int32_t dy) {
  AnyInputEvent event;
  event.mouse = MouseInputEvent{};
  event.mouse.lastX = dx;
  event.mouse.lastY = dy;
  event.mouse.buttonFlags = 0;
  return event;
}

AnyInputEvent MakeButtonMouseEvent(uint16_t buttonFlags) {
  AnyInputEvent event;
  event.mouse = MouseInputEvent{};
  event.mouse.buttonFlags = buttonFlags;
  return event;
}

AnyInputEvent MakeWheelMouseEvent(int16_t delta) {
  AnyInputEvent event;
  event.mouse = MouseInputEvent{};
  event.mouse.buttonFlags = mouse_button_flags::Wheel;
  event.mouse.buttonData = delta;
  return event;
}

}// namespace

TEST_CASE("EventRing FIFO order", "[input_queue]") {
  EventRing ring;
  ring.Push(MakeKeyEvent(1));
  ring.Push(MakeKeyEvent(2));
  ring.Push(MakeKeyEvent(3));

  AnyInputEvent out;
  REQUIRE(ring.Pop(out));
  REQUIRE(out.keyboard.virtualKey == 1);
  REQUIRE(ring.Pop(out));
  REQUIRE(out.keyboard.virtualKey == 2);
  REQUIRE(ring.Pop(out));
  REQUIRE(out.keyboard.virtualKey == 3);
  REQUIRE_FALSE(ring.Pop(out));
}

TEST_CASE("EventRing empty dequeue returns false", "[input_queue]") {
  EventRing ring;
  AnyInputEvent out;
  REQUIRE_FALSE(ring.Pop(out));
}

TEST_CASE("EventRing coalesces the oldest pure-motion mouse event when full", "[input_queue]") {
  EventRing ring;
  for (size_t i = 0; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeMotionMouseEvent(static_cast<int32_t>(i), 0));
  }
  REQUIRE(ring.Full());

  // Push one more motion event: the oldest (dx=0) must be coalesced away,
  // and everything else shifts down by one, preserving FIFO order.
  ring.Push(MakeMotionMouseEvent(999, 0));
  REQUIRE(ring.Count() == EventRing::kCapacity);

  AnyInputEvent out;
  REQUIRE(ring.Pop(out));
  REQUIRE(out.mouse.lastX == 1);// dx=0 was dropped, dx=1 is now oldest
}

TEST_CASE("EventRing never drops keyboard/button/wheel events while a motion event exists", "[input_queue]") {
  EventRing ring;
  // Fill with one coalescable motion event followed by non-droppable events.
  ring.Push(MakeMotionMouseEvent(1, 1));
  for (size_t i = 1; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeKeyEvent(static_cast<uint16_t>(i)));
  }
  REQUIRE(ring.Full());

  // Overflow: the single motion event must be coalesced, never a keyboard
  // event, leaving every keyboard event intact.
  ring.Push(MakeButtonMouseEvent(mouse_button_flags::LeftDown));

  AnyInputEvent out;
  size_t keyCount = 0;
  bool sawButtonEvent = false;
  while (ring.Pop(out)) {
    if (out.base.is<KeyboardInputEvent>()) {
      ++keyCount;
    } else if (out.base.is<MouseInputEvent>() && out.mouse.buttonFlags != 0) {
      sawButtonEvent = true;
    }
  }
  REQUIRE(keyCount == EventRing::kCapacity - 1);
  REQUIRE(sawButtonEvent);
}

TEST_CASE("EventRing falls back to dropping the oldest event when no motion event exists", "[input_queue]") {
  EventRing ring;
  for (size_t i = 0; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeKeyEvent(static_cast<uint16_t>(i)));
  }
  REQUIRE(ring.Full());

  ring.Push(MakeKeyEvent(999));

  AnyInputEvent out;
  REQUIRE(ring.Pop(out));
  REQUIRE(out.keyboard.virtualKey == 1);// vk=0 (oldest) was dropped
}

TEST_CASE("EventRing wheel events are never coalesced away", "[input_queue]") {
  EventRing ring;
  ring.Push(MakeWheelMouseEvent(3));
  for (size_t i = 1; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeMotionMouseEvent(static_cast<int32_t>(i), 0));
  }
  REQUIRE(ring.Full());

  // One more motion event: a coalescable pure-motion event exists (there are
  // plenty), so the wheel event at the front must survive.
  ring.Push(MakeMotionMouseEvent(999, 0));

  AnyInputEvent out;
  REQUIRE(ring.Pop(out));
  REQUIRE(out.mouse.buttonFlags == mouse_button_flags::Wheel);
  REQUIRE(out.mouse.buttonData == 3);
}

TEST_CASE("AnyInputEvent as<T>() returns nullptr for a mismatched type", "[input_queue]") {
  AnyInputEvent event = MakeKeyEvent(42);

  REQUIRE(event.base.as<KeyboardInputEvent>() != nullptr);
  REQUIRE(event.base.as<MouseInputEvent>() == nullptr);

  AnyInputEvent mouseEvent = MakeMotionMouseEvent(1, 2);
  REQUIRE(mouseEvent.base.as<MouseInputEvent>() != nullptr);
  REQUIRE(mouseEvent.base.as<KeyboardInputEvent>() == nullptr);
}
