
#include "../../kernel/src/input/key_translate.hpp"
#include "../../kernel/src/input/scancode_tables.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace kernel::input;
using namespace scancode_tables;

TEST_CASE("scancode_tables base table spot values", "[scancode_tables]") {
  REQUIRE(kSet1BaseToVk[0x1E] == VK_A);                       // 'A' make
  REQUIRE(kSet1BaseToVk[0x10] == static_cast<uint16_t>(VK_A + ('Q' - 'A')));
  REQUIRE(kSet1BaseToVk[0x02] == VK_0 + 1);// '1'
  REQUIRE(kSet1BaseToVk[0x0B] == VK_0 + 0);// '0'
  REQUIRE(kSet1BaseToVk[0x3B] == VK_F1);
  REQUIRE(kSet1BaseToVk[0x44] == VK_F1 + 9);// F10
  REQUIRE(kSet1BaseToVk[0x57] == VK_F1 + 10);// F11
  REQUIRE(kSet1BaseToVk[0x58] == VK_F1 + 11);// F12
  REQUIRE(kSet1BaseToVk[0x1C] == VK_RETURN);
  REQUIRE(kSet1BaseToVk[0x39] == VK_SPACE);
}

TEST_CASE("scancode_tables unmapped codes yield VK_NONE", "[scancode_tables]") {
  REQUIRE(kSet1BaseToVk[0x00] == VK_NONE);
  REQUIRE(kSet1BaseToVk[0x54] == VK_NONE);
  REQUIRE(kSet1BaseToVk[0x55] == VK_NONE);
  REQUIRE(kSet1BaseToVk[0x56] == VK_NONE);
  REQUIRE(kSet1BaseToVk[0x7F] == VK_NONE);
  REQUIRE(LookupExtendedVk(0x00) == VK_NONE);
  REQUIRE(LookupExtendedVk(0x1E) == VK_NONE);// 'A' has no extended form
}

TEST_CASE("scancode_tables extended cluster", "[scancode_tables]") {
  REQUIRE(LookupExtendedVk(0x1C) == VK_RETURN);// KP Enter
  REQUIRE(LookupExtendedVk(0x1D) == VK_RCONTROL);
  REQUIRE(LookupExtendedVk(0x38) == VK_RMENU);
  REQUIRE(LookupExtendedVk(0x47) == VK_HOME);
  REQUIRE(LookupExtendedVk(0x48) == VK_UP);
  REQUIRE(LookupExtendedVk(0x49) == VK_PRIOR);
  REQUIRE(LookupExtendedVk(0x4B) == VK_LEFT);
  REQUIRE(LookupExtendedVk(0x4D) == VK_RIGHT);
  REQUIRE(LookupExtendedVk(0x4F) == VK_END);
  REQUIRE(LookupExtendedVk(0x50) == VK_DOWN);
  REQUIRE(LookupExtendedVk(0x51) == VK_NEXT);
  REQUIRE(LookupExtendedVk(0x52) == VK_INSERT);
  REQUIRE(LookupExtendedVk(0x53) == VK_DELETE);
  REQUIRE(LookupExtendedVk(0x5B) == VK_LWIN);
  REQUIRE(LookupExtendedVk(0x5C) == VK_RWIN);
  REQUIRE(LookupExtendedVk(0x5D) == VK_APPS);
  REQUIRE(LookupExtendedVk(0x35) == VK_DIVIDE);// KP /
  REQUIRE(LookupExtendedVk(0x37) == VK_SNAPSHOT);
  REQUIRE(LookupExtendedVk(0x46) == VK_CANCEL);// Ctrl+Break
}

TEST_CASE("scancode_tables base table has no accidental duplicate VKs for distinct keys", "[scancode_tables]") {
  for (size_t i = 0; i < kSet1BaseToVk.size(); i++) {
    if (kSet1BaseToVk[i] == VK_NONE) continue;
    for (size_t j = i + 1; j < kSet1BaseToVk.size(); j++) {
      if (kSet1BaseToVk[j] == VK_NONE) continue;
      REQUIRE(kSet1BaseToVk[i] != kSet1BaseToVk[j]);
    }
  }
}

TEST_CASE("KeyTranslator make and break", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x1E, event));// 'A' make
  REQUIRE(event.virtualKey == VK_A);
  REQUIRE((event.keyFlags & key_flags::Break) == 0);
  REQUIRE((event.keyFlags & key_flags::Extended) == 0);

  REQUIRE(translator.Feed(0x9E, event));// 'A' break
  REQUIRE(event.virtualKey == VK_A);
  REQUIRE((event.keyFlags & key_flags::Break) != 0);
}

TEST_CASE("KeyTranslator extended prefix", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE_FALSE(translator.Feed(0xE0, event));// prefix byte alone: no event
  REQUIRE(translator.Feed(0x48, event));       // E0 48 -> Up arrow
  REQUIRE(event.virtualKey == VK_UP);
  REQUIRE((event.keyFlags & key_flags::Extended) != 0);
  REQUIRE((event.keyFlags & key_flags::Break) == 0);
}

TEST_CASE("KeyTranslator a bare E0 followed by another E0 does not desync", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE_FALSE(translator.Feed(0xE0, event));
  REQUIRE_FALSE(translator.Feed(0xE0, event));// still pending, no event yet
  REQUIRE(translator.Feed(0x4B, event));       // E0 4B -> Left arrow
  REQUIRE(event.virtualKey == VK_LEFT);
  REQUIRE((event.keyFlags & key_flags::Extended) != 0);
}

TEST_CASE("KeyTranslator pause sequence produces at most one event", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  int eventCount = 0;
  const uint8_t pauseSequence[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
  for (uint8_t b: pauseSequence) {
    if (translator.Feed(b, event)) eventCount++;
  }

  REQUIRE(eventCount == 1);
  REQUIRE(event.virtualKey == VK_PAUSE);
}

TEST_CASE("KeyTranslator ignores junk protocol bytes without corrupting the next valid code", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE_FALSE(translator.Feed(0xFF, event));
  REQUIRE_FALSE(translator.Feed(0xFA, event));
  REQUIRE_FALSE(translator.Feed(0xEE, event));
  REQUIRE(translator.Feed(0x1E, event));// 'A' make still decodes cleanly
  REQUIRE(event.virtualKey == VK_A);
  REQUIRE((event.keyFlags & key_flags::Break) == 0);
}

TEST_CASE("KeyTranslator typematic repeat flag", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x1E, event));// first make: not a repeat
  REQUIRE((event.keyFlags & key_flags::Repeat) == 0);

  REQUIRE(translator.Feed(0x1E, event));// held: typematic repeat
  REQUIRE((event.keyFlags & key_flags::Repeat) != 0);

  REQUIRE(translator.Feed(0x9E, event));// break clears the down bit
  REQUIRE((event.keyFlags & key_flags::Break) != 0);

  REQUIRE(translator.Feed(0x1E, event));// re-make after a break: not a repeat
  REQUIRE((event.keyFlags & key_flags::Repeat) == 0);
}

TEST_CASE("KeyTranslator Shift derives shifted character", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x2A, event));// LShift make
  REQUIRE(translator.Feed(0x02, event));// '1' with Shift held
  REQUIRE(event.character == '!');
}

TEST_CASE("KeyTranslator CapsLock applies to letters only", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x3A, event));// CapsLock make (toggles on)
  REQUIRE(translator.Feed(0x1E, event));// 'a' -> 'A' under CapsLock
  REQUIRE(event.character == 'A');

  REQUIRE(translator.Feed(0x02, event));// '1' unaffected by CapsLock
  REQUIRE(event.character == '1');
}

TEST_CASE("KeyTranslator CapsLock + Shift cancels out on letters", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x3A, event));// CapsLock on
  REQUIRE(translator.Feed(0x2A, event));// LShift make
  REQUIRE(translator.Feed(0x1E, event));// 'a' with both CapsLock and Shift -> lowercase
  REQUIRE(event.character == 'a');
}

TEST_CASE("KeyTranslator Ctrl state persists in modifiers across an intervening key", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x1D, event));// LCtrl make
  REQUIRE((event.modifiers & modifier_flags::LControl) != 0);

  REQUIRE(translator.Feed(0x1E, event));// unrelated key while Ctrl held
  REQUIRE((event.modifiers & modifier_flags::LControl) != 0);

  REQUIRE(translator.Feed(0x9D, event));// LCtrl break
  REQUIRE((event.modifiers & modifier_flags::LControl) == 0);
}

TEST_CASE("KeyTranslator NumLock gates keypad digit characters", "[key_translate]") {
  KeyTranslator translator;
  KeyboardInputEvent event{};

  REQUIRE(translator.Feed(0x47, event));// KP7/Home make, NumLock off: no character
  REQUIRE(event.character == 0);

  REQUIRE(translator.Feed(0x45, event));// NumLock make (toggles on)
  REQUIRE(translator.Feed(0x47, event));// KP7 with NumLock on -> '7'
  REQUIRE(event.character == '7');

  KeyboardInputEvent navEvent{};
  REQUIRE_FALSE(translator.Feed(0xE0, navEvent));
  REQUIRE(translator.Feed(0x47, navEvent));// E0 47 is always Home, never a digit
  REQUIRE(navEvent.virtualKey == VK_HOME);
  REQUIRE(navEvent.character == 0);
}
