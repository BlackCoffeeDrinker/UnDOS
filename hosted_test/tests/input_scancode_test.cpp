
#include "ps2_hid_reports.hpp"
#include "ps2_key_translate.hpp"
#include "ps2_scancode_tables.hpp"

#include <catch2/catch_test_macros.hpp>

// The PS/2 half of the keyboard path: scancode set 1 in, HID Keyboard/Keypad
// usages out. What a usage *means* -- virtual key, ASCII, modifier state --
// is asserted in hid_parser_test.cpp, so between the two suites the original
// end-to-end scancode -> VK/character expectations are still pinned.
using namespace ps2;
using namespace kernel::hid::usage_kbd;

TEST_CASE("ps2_scancode_tables base table spot values", "[scancode_tables]") {
  REQUIRE(kSet1BaseToUsage[0x1E] == A);
  REQUIRE(kSet1BaseToUsage[0x10] == static_cast<uint16_t>(A + ('Q' - 'A')));
  REQUIRE(kSet1BaseToUsage[0x02] == Num1);
  REQUIRE(kSet1BaseToUsage[0x0B] == Num0);
  REQUIRE(kSet1BaseToUsage[0x3B] == F1);
  REQUIRE(kSet1BaseToUsage[0x44] == F1 + 9); // F10
  REQUIRE(kSet1BaseToUsage[0x57] == F1 + 10);// F11
  REQUIRE(kSet1BaseToUsage[0x58] == F1 + 11);// F12
  REQUIRE(kSet1BaseToUsage[0x1C] == Enter);
  REQUIRE(kSet1BaseToUsage[0x39] == Space);
}

TEST_CASE("ps2_scancode_tables unmapped codes yield None", "[scancode_tables]") {
  REQUIRE(kSet1BaseToUsage[0x00] == None);
  REQUIRE(kSet1BaseToUsage[0x54] == None);
  REQUIRE(kSet1BaseToUsage[0x55] == None);
  REQUIRE(kSet1BaseToUsage[0x56] == None);
  REQUIRE(kSet1BaseToUsage[0x7F] == None);
  REQUIRE(LookupExtendedUsage(0x00) == None);
  REQUIRE(LookupExtendedUsage(0x1E) == None);// 'A' has no extended form
}

TEST_CASE("ps2_scancode_tables extended cluster", "[scancode_tables]") {
  REQUIRE(LookupExtendedUsage(0x1C) == KpEnter);
  REQUIRE(LookupExtendedUsage(0x1D) == RightControl);
  REQUIRE(LookupExtendedUsage(0x38) == RightAlt);
  REQUIRE(LookupExtendedUsage(0x47) == Home);
  REQUIRE(LookupExtendedUsage(0x48) == Up);
  REQUIRE(LookupExtendedUsage(0x49) == PageUp);
  REQUIRE(LookupExtendedUsage(0x4B) == Left);
  REQUIRE(LookupExtendedUsage(0x4D) == Right);
  REQUIRE(LookupExtendedUsage(0x4F) == End);
  REQUIRE(LookupExtendedUsage(0x50) == Down);
  REQUIRE(LookupExtendedUsage(0x51) == PageDown);
  REQUIRE(LookupExtendedUsage(0x52) == Insert);
  REQUIRE(LookupExtendedUsage(0x53) == Delete);
  REQUIRE(LookupExtendedUsage(0x5B) == LeftGui);
  REQUIRE(LookupExtendedUsage(0x5C) == RightGui);
  REQUIRE(LookupExtendedUsage(0x5D) == Application);
  REQUIRE(LookupExtendedUsage(0x35) == KpDivide);
  REQUIRE(LookupExtendedUsage(0x37) == PrintScreen);
  REQUIRE(LookupExtendedUsage(0x46) == Cancel);// Ctrl+Break
}

TEST_CASE("ps2_scancode_tables base table has no duplicate usages for distinct keys", "[scancode_tables]") {
  for (size_t i = 0; i < kSet1BaseToUsage.size(); i++) {
    if (kSet1BaseToUsage[i] == None) continue;
    for (size_t j = i + 1; j < kSet1BaseToUsage.size(); j++) {
      if (kSet1BaseToUsage[j] == None) continue;
      REQUIRE(kSet1BaseToUsage[i] != kSet1BaseToUsage[j]);
    }
  }
}

TEST_CASE("KeyTranslator make and break", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  REQUIRE(translator.Feed(0x1E, transition));// 'A' make
  REQUIRE(transition.usage == A);
  REQUIRE_FALSE(transition.isBreak);

  REQUIRE(translator.Feed(0x9E, transition));// 'A' break
  REQUIRE(transition.usage == A);
  REQUIRE(transition.isBreak);
}

TEST_CASE("KeyTranslator extended prefix", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  REQUIRE_FALSE(translator.Feed(0xE0, transition));// prefix byte alone
  REQUIRE(translator.Feed(0x48, transition));      // E0 48 -> Up arrow
  REQUIRE(transition.usage == Up);
  REQUIRE_FALSE(transition.isBreak);
}

TEST_CASE("KeyTranslator a bare E0 followed by another E0 does not desync", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  REQUIRE_FALSE(translator.Feed(0xE0, transition));
  REQUIRE_FALSE(translator.Feed(0xE0, transition));// still pending, no event yet
  REQUIRE(translator.Feed(0x4B, transition));      // E0 4B -> Left arrow
  REQUIRE(transition.usage == Left);
}

TEST_CASE("KeyTranslator pause sequence produces at most one event", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  int eventCount = 0;
  const uint8_t pauseSequence[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
  for (const uint8_t b: pauseSequence) {
    if (translator.Feed(b, transition)) eventCount++;
  }

  REQUIRE(eventCount == 1);
  REQUIRE(transition.usage == Pause);
  REQUIRE_FALSE(transition.isBreak);
}

TEST_CASE("KeyTranslator ignores junk protocol bytes without corrupting the next valid code", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  REQUIRE_FALSE(translator.Feed(0xFF, transition));
  REQUIRE_FALSE(translator.Feed(0xFA, transition));
  REQUIRE_FALSE(translator.Feed(0xEE, transition));
  REQUIRE(translator.Feed(0x1E, transition));// 'A' make still decodes cleanly
  REQUIRE(transition.usage == A);
  REQUIRE_FALSE(transition.isBreak);
}

TEST_CASE("KeyTranslator distinguishes the keypad from the navigation cluster", "[key_translate]") {
  KeyTranslator translator;
  KeyTransition transition{};

  REQUIRE(translator.Feed(0x47, transition));// bare 0x47 is KP 7
  REQUIRE(transition.usage == Kp1 + 6);

  REQUIRE_FALSE(translator.Feed(0xE0, transition));
  REQUIRE(translator.Feed(0x47, transition));// E0 47 is Home
  REQUIRE(transition.usage == Home);
}

TEST_CASE("KeyboardReportBuilder renders the held set as a HID report", "[key_translate]") {
  KeyboardReportBuilder builder;
  uint8_t report[kKeyReportBytes]{};

  // A held modifier lands in the modifier byte, not a key slot.
  REQUIRE(builder.Apply(KeyTransition{LeftShift, false}));
  builder.Pack(report);
  REQUIRE(report[0] == kernel::hid::kbd_modifier_bit::LeftShift);
  REQUIRE(report[2] == 0);

  REQUIRE(builder.Apply(KeyTransition{A, false}));
  builder.Pack(report);
  REQUIRE(report[2] == A);

  // A second make of a key already down changes nothing, so no report is
  // produced for a typematic repeat.
  REQUIRE_FALSE(builder.Apply(KeyTransition{A, false}));

  REQUIRE(builder.Apply(KeyTransition{A, true}));
  builder.Pack(report);
  REQUIRE(report[2] == 0);
  REQUIRE(report[0] == kernel::hid::kbd_modifier_bit::LeftShift);

  // A break for a key that was never down is not a state change either.
  REQUIRE_FALSE(builder.Apply(KeyTransition{Escape, true}));
}

TEST_CASE("KeyboardReportBuilder drops overflow beyond its slot count", "[key_translate]") {
  KeyboardReportBuilder builder;

  // Fill every slot with a distinct usage, then one more.
  for (size_t i = 0; i < kKeySlots; ++i) {
    REQUIRE(builder.Apply(KeyTransition{static_cast<uint16_t>(A + i), false}));
  }
  REQUIRE_FALSE(builder.Apply(KeyTransition{Escape, false}));

  // The already-held keys must all still be reported.
  uint8_t report[kKeyReportBytes]{};
  builder.Pack(report);
  for (size_t i = 0; i < kKeySlots; ++i) {
    REQUIRE(report[2 + i] == static_cast<uint8_t>(A + i));
  }
}
