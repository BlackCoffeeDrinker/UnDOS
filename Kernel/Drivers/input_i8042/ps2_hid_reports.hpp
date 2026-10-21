#pragma once

#include <stdint.h>

#include <kernel/input.hpp>

#include "ps2_key_translate.hpp"
#include "ps2_mouse_packet.hpp"

// The i8042's synthetic HID face. Each PS/2 device publishes a static
// report descriptor and packs its decoded state into report bytes, exactly
// as a USB interrupt endpoint would, so drivers/input_hid/ is the only
// place that maps controls onto events. Header-only and freestanding.
namespace ps2 {

// Deliberately 10 key slots rather than the USB boot protocol's six:
// nothing requires UnDOS's descriptor to be the boot descriptor, and a PS/2
// keyboard can physically report more simultaneous keys than 6KRO allows.
constexpr size_t kKeySlots = 10;
constexpr size_t kKeyReportBytes = 2 + kKeySlots;// modifier byte, reserved, slots

// Keyboard: 8 modifier bits, one constant byte, then kKeySlots array slots
// each carrying a Keyboard/Keypad usage ID.
constexpr uint8_t kKeyboardReportDescriptor[] = {
    0x05, 0x01,      // Usage Page (Generic Desktop)
    0x09, 0x06,      // Usage (Keyboard)
    0xA1, 0x01,      // Collection (Application)
    0x05, 0x07,      //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,      //   Usage Minimum (Left Control)
    0x29, 0xE7,      //   Usage Maximum (Right GUI)
    0x15, 0x00,      //   Logical Minimum (0)
    0x25, 0x01,      //   Logical Maximum (1)
    0x75, 0x01,      //   Report Size (1)
    0x95, 0x08,      //   Report Count (8)
    0x81, 0x02,      //   Input (Data, Variable, Absolute)
    0x95, 0x01,      //   Report Count (1)
    0x75, 0x08,      //   Report Size (8)
    0x81, 0x03,      //   Input (Constant) -- reserved byte
    0x15, 0x00,      //   Logical Minimum (0)
    0x26, 0xFF, 0x00,//   Logical Maximum (255)
    0x19, 0x00,      //   Usage Minimum (0)
    0x29, 0xFF,      //   Usage Maximum (255)
    0x75, 0x08,      //   Report Size (8)
    0x95, static_cast<uint8_t>(kKeySlots),//   Report Count (kKeySlots)
    0x81, 0x00,      //   Input (Data, Array, Absolute)
    0xC0,            // End Collection
};

constexpr size_t kMouseReportBytes = 4;

// Mouse: 5 buttons, three padding bits, then relative X/Y/wheel.
constexpr uint8_t kMouseReportDescriptor[] = {
    0x05, 0x01,// Usage Page (Generic Desktop)
    0x09, 0x02,// Usage (Mouse)
    0xA1, 0x01,// Collection (Application)
    0x09, 0x01,//   Usage (Pointer)
    0xA1, 0x00,//   Collection (Physical)
    0x05, 0x09,//     Usage Page (Button)
    0x19, 0x01,//     Usage Minimum (Button 1)
    0x29, 0x05,//     Usage Maximum (Button 5)
    0x15, 0x00,//     Logical Minimum (0)
    0x25, 0x01,//     Logical Maximum (1)
    0x95, 0x05,//     Report Count (5)
    0x75, 0x01,//     Report Size (1)
    0x81, 0x02,//     Input (Data, Variable, Absolute)
    0x95, 0x01,//     Report Count (1)
    0x75, 0x03,//     Report Size (3)
    0x81, 0x03,//     Input (Constant) -- padding
    0x05, 0x01,//     Usage Page (Generic Desktop)
    0x09, 0x30,//     Usage (X)
    0x09, 0x31,//     Usage (Y)
    0x15, 0x81,//     Logical Minimum (-127)
    0x25, 0x7F,//     Logical Maximum (127)
    0x75, 0x08,//     Report Size (8)
    0x95, 0x02,//     Report Count (2)
    0x81, 0x06,//     Input (Data, Variable, Relative)
    0x09, 0x38,//     Usage (Wheel)
    0x95, 0x01,//     Report Count (1)
    0x81, 0x06,//     Input (Data, Variable, Relative)
    0xC0,      //   End Collection
    0xC0,      // End Collection
};

// Tracks which usages are currently held and renders them as a HID
// keyboard report. The PS/2 stream is a transition stream and HID is a
// state stream, so this is where make/break becomes "set of keys down" --
// and drivers/input_hid/ turns it straight back into transitions. That
// round trip is the price of having exactly one mapping code path.
class KeyboardReportBuilder {
  public:
  // Applies one decoded transition. Returns false when the transition
  // changed nothing (an unknown break, or a make with every slot taken),
  // in which case no report needs to be sent.
  bool Apply(const KeyTransition &transition) noexcept {
    const uint8_t modifierBit = ModifierBit(transition.usage);
    if (modifierBit != 0) {
      const uint8_t updated = transition.isBreak ? static_cast<uint8_t>(m_modifiers & ~modifierBit)
                                                 : static_cast<uint8_t>(m_modifiers | modifierBit);
      if (updated == m_modifiers) return false;
      m_modifiers = updated;
      return true;
    }

    for (size_t i = 0; i < kKeySlots; ++i) {
      if (m_slots[i] != transition.usage) continue;
      if (!transition.isBreak) return false;// already held: typematic repeat
      m_slots[i] = 0;
      return true;
    }
    if (transition.isBreak) return false;// break for a key we never saw down

    for (size_t i = 0; i < kKeySlots; ++i) {
      if (m_slots[i] != 0) continue;
      m_slots[i] = transition.usage;
      return true;
    }
    return false;// more simultaneous keys than slots: drop deterministically
  }

  // The Pause key has no break code at all, so it is reported as a make
  // immediately followed by a release.
  void Release(uint16_t usage) noexcept {
    for (size_t i = 0; i < kKeySlots; ++i) {
      if (m_slots[i] == usage) m_slots[i] = 0;
    }
  }

  void Pack(uint8_t (&report)[kKeyReportBytes]) const noexcept {
    report[0] = m_modifiers;
    report[1] = 0;
    for (size_t i = 0; i < kKeySlots; ++i) report[2 + i] = static_cast<uint8_t>(m_slots[i]);
  }

  private:
  static constexpr uint8_t ModifierBit(uint16_t usage) noexcept {
    using namespace kernel::hid::usage_kbd;
    if (usage < LeftControl || usage > RightGui) return 0;
    return static_cast<uint8_t>(1u << (usage - LeftControl));
  }

  uint8_t m_modifiers = 0;
  uint16_t m_slots[kKeySlots] = {};
};

// Clamps a decoded axis delta into the signed byte the descriptor declares.
constexpr int8_t ClampToReportAxis(int32_t value) noexcept {
  if (value > 127) return 127;
  if (value < -127) return -127;
  return static_cast<int8_t>(value);
}

inline void PackMouseReport(const kernel::input::MousePacket &packet,
                            uint8_t (&report)[kMouseReportBytes]) noexcept {
  report[0] = static_cast<uint8_t>(packet.buttons & 0x1F);
  report[1] = static_cast<uint8_t>(ClampToReportAxis(packet.dx));
  report[2] = static_cast<uint8_t>(ClampToReportAxis(packet.dy));
  report[3] = static_cast<uint8_t>(packet.wheel);
}

}// namespace ps2
