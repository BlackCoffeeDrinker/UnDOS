
#include "../../../Kernel/Source/src/input/event_ring.hpp"

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

TEST_CASE("EventRing coalesces motion into the newest unread entry when full", "[input_queue]") {
  EventRing ring;
  for (size_t i = 0; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeMotionMouseEvent(1, 2));
  }
  REQUIRE(ring.Full());

  // Overflowing motion accumulates into the newest unread motion entry
  // rather than consuming a slot, so nothing older is displaced and the
  // total displacement is preserved.
  ring.Push(MakeMotionMouseEvent(10, 20));
  ring.Push(MakeMotionMouseEvent(100, 200));
  REQUIRE(ring.Count() == EventRing::kCapacity);

  AnyInputEvent out;
  int32_t totalX = 0;
  int32_t totalY = 0;
  while (ring.Pop(out)) {
    totalX += out.mouse.lastX;
    totalY += out.mouse.lastY;
  }
  REQUIRE(totalX == static_cast<int32_t>(EventRing::kCapacity) * 1 + 10 + 100);
  REQUIRE(totalY == static_cast<int32_t>(EventRing::kCapacity) * 2 + 20 + 200);
}

TEST_CASE("EventRing preserves order and indices across a wrap-around", "[input_queue]") {
  EventRing ring;
  AnyInputEvent out;

  // Push and pop several times the capacity, so head/tail wrap repeatedly.
  for (uint16_t round = 0; round < 5; ++round) {
    for (size_t i = 0; i < EventRing::kCapacity - 1; ++i) {
      ring.Push(MakeKeyEvent(static_cast<uint16_t>(i)));
    }
    for (size_t i = 0; i < EventRing::kCapacity - 1; ++i) {
      REQUIRE(ring.Pop(out));
      REQUIRE(out.keyboard.virtualKey == static_cast<uint16_t>(i));
    }
    REQUIRE(ring.Empty());
  }
}

TEST_CASE("EventRing keeps a button transition when the ring is full of motion", "[input_queue]") {
  EventRing ring;
  for (size_t i = 0; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeMotionMouseEvent(1, 1));
  }
  REQUIRE(ring.Full());

  // A click must never be absorbed into a motion entry: it appends,
  // displacing the oldest motion event instead.
  ring.Push(MakeButtonMouseEvent(mouse_button_flags::LeftDown));

  AnyInputEvent out;
  bool sawButtonEvent = false;
  while (ring.Pop(out)) {
    if (out.mouse.buttonFlags == mouse_button_flags::LeftDown) sawButtonEvent = true;
  }
  REQUIRE(sawButtonEvent);
}

TEST_CASE("EventRing keeps a keyboard event when the ring is full of motion", "[input_queue]") {
  EventRing ring;
  for (size_t i = 0; i < EventRing::kCapacity; ++i) {
    ring.Push(MakeMotionMouseEvent(1, 1));
  }
  REQUIRE(ring.Full());

  ring.Push(MakeKeyEvent(0x41));

  AnyInputEvent out;
  bool sawKey = false;
  while (ring.Pop(out)) {
    if (out.base.is<KeyboardInputEvent>() && out.keyboard.virtualKey == 0x41) sawKey = true;
  }
  REQUIRE(sawKey);
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
