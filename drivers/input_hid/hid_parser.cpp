
#include "hid_parser.hpp"

#include "hid_keyboard_usages.hpp"

namespace {

using namespace kernel::hid;

// Short-item header: bits 7:4 tag, 3:2 type, 1:0 size code. Size code 3
// means 4 data bytes, every other code means that many bytes. Long items
// (header 0xFE) are not emitted by any descriptor this OS parses and are
// rejected rather than skipped, so a malformed stream cannot be silently
// resynchronised into something plausible.
constexpr uint8_t kItemTypeMain = 0;
constexpr uint8_t kItemTypeGlobal = 1;
constexpr uint8_t kItemTypeLocal = 2;

constexpr uint8_t kMainInput = 0x8;
constexpr uint8_t kMainCollection = 0xA;
constexpr uint8_t kMainEndCollection = 0xC;

constexpr uint8_t kGlobalUsagePage = 0x0;
constexpr uint8_t kGlobalLogicalMin = 0x1;
constexpr uint8_t kGlobalLogicalMax = 0x2;
constexpr uint8_t kGlobalReportSize = 0x7;
constexpr uint8_t kGlobalReportId = 0x8;
constexpr uint8_t kGlobalReportCount = 0x9;

constexpr uint8_t kLocalUsage = 0x0;
constexpr uint8_t kLocalUsageMin = 0x1;
constexpr uint8_t kLocalUsageMax = 0x2;

// Input item data bits (HID 1.11 6.2.2.5).
constexpr uint32_t kInputConstant = 1 << 0;
constexpr uint32_t kInputVariable = 1 << 1;
constexpr uint32_t kInputRelative = 1 << 2;

constexpr size_t kMaxLocalUsages = 32;

int32_t SignedItemData(const uint8_t *data, uint8_t size) noexcept {
  switch (size) {
    case 1: return static_cast<int8_t>(data[0]);
    case 2: return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
    case 4:
      return static_cast<int32_t>(static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                                  (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24));
    default: return 0;
  }
}

uint32_t UnsignedItemData(const uint8_t *data, uint8_t size) noexcept {
  uint32_t value = 0;
  for (uint8_t i = 0; i < size; ++i) value |= static_cast<uint32_t>(data[i]) << (8 * i);
  return value;
}

// Linear scale of a raw field value from its declared logical range into
// GamepadState's fixed signed range, so an 8-bit pad and a timed analog
// read are directly comparable. Done in 64-bit to keep the product exact.
int16_t NormalizeAxis(int32_t value, int32_t logicalMin, int32_t logicalMax) noexcept {
  using kernel::input::GamepadState;
  if (logicalMax <= logicalMin) return 0;
  if (value <= logicalMin) return GamepadState::kAxisMin;
  if (value >= logicalMax) return GamepadState::kAxisMax;

  const int64_t span = static_cast<int64_t>(logicalMax) - logicalMin;
  constexpr int64_t outSpan = static_cast<int64_t>(GamepadState::kAxisMax) - GamepadState::kAxisMin;
  const int64_t scaled = ((static_cast<int64_t>(value) - logicalMin) * outSpan) / span + GamepadState::kAxisMin;
  return static_cast<int16_t>(scaled);
}

// Which gamepad_axis slot a Generic Desktop axis usage occupies, or
// gamepad_axis::Count for a usage this layer does not carry.
size_t GamepadAxisSlot(uint16_t usage) noexcept {
  using namespace kernel::input;
  switch (usage) {
    case usage_gd::X: return gamepad_axis::LeftX;
    case usage_gd::Y: return gamepad_axis::LeftY;
    case usage_gd::Rx: return gamepad_axis::RightX;
    case usage_gd::Ry: return gamepad_axis::RightY;
    case usage_gd::Z: return gamepad_axis::LeftTrigger;
    case usage_gd::Rz: return gamepad_axis::RightTrigger;
    default: return gamepad_axis::Count;
  }
}

// HID hat switch: 0 = north, increasing clockwise in 45-degree steps; any
// value outside the logical range means "centred".
void DecodeHat(int32_t value, int32_t logicalMin, int32_t logicalMax, int8_t &hatX, int8_t &hatY) noexcept {
  if (value < logicalMin || value > logicalMax) {
    hatX = 0;
    hatY = 0;
    return;
  }
  constexpr int8_t xs[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  constexpr int8_t ys[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  const int32_t index = (value - logicalMin) & 7;
  hatX = xs[index];
  hatY = ys[index];
}

// Usages whose PS/2 origin was an E0-prefixed scancode. Reported as
// key_flags::Extended so consumers that cared about the distinction (the
// navigation cluster vs. the keypad) keep seeing it.
bool IsExtendedUsage(uint16_t usage) noexcept {
  switch (usage) {
    case usage_kbd::RightControl:
    case usage_kbd::RightAlt:
    case usage_kbd::LeftGui:
    case usage_kbd::RightGui:
    case usage_kbd::Application:
    case usage_kbd::PrintScreen:
    case usage_kbd::Cancel:
    case usage_kbd::KpDivide:
    case usage_kbd::KpEnter:
    case usage_kbd::Insert:
    case usage_kbd::Home:
    case usage_kbd::PageUp:
    case usage_kbd::Delete:
    case usage_kbd::End:
    case usage_kbd::PageDown:
    case usage_kbd::Right:
    case usage_kbd::Left:
    case usage_kbd::Down:
    case usage_kbd::Up:
      return true;
    default:
      return false;
  }
}

bool ContainsUsage(const uint16_t *list, size_t count, uint16_t usage) noexcept {
  for (size_t i = 0; i < count; ++i) {
    if (list[i] == usage) return true;
  }
  return false;
}

}// namespace

namespace kernel::hid {

bool ParseReportDescriptor(kstd::span<const uint8_t> descriptor, ReportFormat &out) {
  ReportFormat parsed{};

  uint16_t usagePage = 0;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
  uint32_t reportSize = 0;
  uint32_t reportCount = 0;

  uint16_t localUsages[kMaxLocalUsages]{};
  size_t localUsageCount = 0;
  bool haveUsageRange = false;
  uint16_t usageMin = 0;
  uint16_t usageMax = 0;

  uint32_t bitOffset = 0;
  int collectionDepth = 0;
  bool haveCollectionUsage = false;

  size_t i = 0;
  while (i < descriptor.size()) {
    const uint8_t header = descriptor[i++];
    if (header == 0xFE) return false;// long item: unsupported, never synthesized

    const auto tag = static_cast<uint8_t>(header >> 4);
    const auto type = static_cast<uint8_t>((header >> 2) & 0x3);
    const auto sizeCode = static_cast<uint8_t>(header & 0x3);
    const uint8_t size = (sizeCode == 3) ? 4 : sizeCode;

    if (i + size > descriptor.size()) return false;// truncated item
    const uint8_t *data = descriptor.data() + i;
    i += size;

    if (type == kItemTypeGlobal) {
      switch (tag) {
        case kGlobalUsagePage: usagePage = static_cast<uint16_t>(UnsignedItemData(data, size)); break;
        case kGlobalLogicalMin: logicalMin = SignedItemData(data, size); break;
        case kGlobalLogicalMax: logicalMax = SignedItemData(data, size); break;
        case kGlobalReportSize: reportSize = UnsignedItemData(data, size); break;
        case kGlobalReportId: parsed.reportId = static_cast<uint8_t>(UnsignedItemData(data, size)); break;
        case kGlobalReportCount: reportCount = UnsignedItemData(data, size); break;
        default: break;// physical min/max, unit, exponent: not used by this layer
      }
      continue;
    }

    if (type == kItemTypeLocal) {
      switch (tag) {
        case kLocalUsage:
          if (localUsageCount >= kMaxLocalUsages) return false;
          localUsages[localUsageCount++] = static_cast<uint16_t>(UnsignedItemData(data, size));
          break;
        case kLocalUsageMin:
          usageMin = static_cast<uint16_t>(UnsignedItemData(data, size));
          haveUsageRange = true;
          break;
        case kLocalUsageMax:
          usageMax = static_cast<uint16_t>(UnsignedItemData(data, size));
          haveUsageRange = true;
          break;
        default: break;
      }
      continue;
    }

    if (type != kItemTypeMain) return false;// reserved item type

    switch (tag) {
      case kMainCollection: {
        ++collectionDepth;
        // The outermost Generic Desktop collection usage is what tells the
        // decoder whether this is a keyboard, a mouse or a stick.
        if (!haveCollectionUsage && usagePage == usage_page::GenericDesktop && localUsageCount > 0) {
          parsed.collectionUsage = localUsages[0];
          haveCollectionUsage = true;
        }
        break;
      }
      case kMainEndCollection:
        if (--collectionDepth < 0) return false;
        break;
      case kMainInput: {
        const uint32_t flags = UnsignedItemData(data, size);
        if (reportSize == 0 || reportSize > 32) return false;
        for (uint32_t n = 0; n < reportCount; ++n) {
          if (bitOffset + reportSize > 0xFFFFu) return false;
          if ((flags & kInputConstant) == 0) {
            if (parsed.fieldCount >= ReportFormat::kMaxFields) return false;
            ReportField &field = parsed.fields[parsed.fieldCount++];
            field.usagePage = usagePage;
            field.bitOffset = static_cast<uint16_t>(bitOffset);
            field.bitSize = static_cast<uint8_t>(reportSize);
            field.isRelative = (flags & kInputRelative) != 0;
            field.isArray = (flags & kInputVariable) == 0;
            field.logicalMin = logicalMin;
            field.logicalMax = logicalMax;
            if (!field.isArray) {
              if (n < localUsageCount) {
                field.usage = localUsages[n];
              } else if (haveUsageRange && usageMin + n <= usageMax) {
                field.usage = static_cast<uint16_t>(usageMin + n);
              } else if (localUsageCount > 0) {
                field.usage = localUsages[localUsageCount - 1];
              }
            }
          }
          bitOffset += reportSize;
        }
        break;
      }
      default: break;// Output/Feature: this layer only consumes input reports
    }

    // Local items apply to the main item that follows them and nothing else.
    localUsageCount = 0;
    haveUsageRange = false;
    usageMin = 0;
    usageMax = 0;
  }

  if (collectionDepth != 0) return false;// unterminated collection
  if (parsed.fieldCount == 0) return false;

  parsed.reportBytes = (bitOffset + 7) / 8;
  out = parsed;
  return true;
}

int32_t ExtractField(const ReportField &field, kstd::span<const uint8_t> report) noexcept {
  uint32_t raw = 0;
  for (uint8_t bit = 0; bit < field.bitSize; ++bit) {
    const uint32_t absoluteBit = static_cast<uint32_t>(field.bitOffset) + bit;
    const size_t byteIndex = absoluteBit / 8;
    if (byteIndex >= report.size()) break;
    if (report[byteIndex] & (1u << (absoluteBit % 8))) raw |= 1u << bit;
  }

  // Sign-extend only when the declared range says the control is signed;
  // an unsigned 8-bit axis must not turn negative at 0x80.
  if (field.logicalMin < 0 && field.bitSize < 32 && (raw & (1u << (field.bitSize - 1)))) {
    raw |= ~((1u << field.bitSize) - 1u);
  }
  return static_cast<int32_t>(raw);
}

input::KeyboardInputEvent MakeKeyEvent(uint16_t usage, bool isBreak, uint32_t modifiers) noexcept {
  using namespace kernel::input;
  using namespace vk;

  KeyboardInputEvent event;
  event.virtualKey = (usage < 256) ? kUsageToVk[usage] : VK_NONE;
  event.scanCode = static_cast<uint8_t>(usage);
  event.modifiers = modifiers;
  if (isBreak) event.keyFlags |= key_flags::Break;
  if (IsExtendedUsage(usage)) event.keyFlags |= key_flags::Extended;

  if (isBreak) return event;// only make events carry a character

  if (const uint16_t virtualKey = event.virtualKey;
      virtualKey >= VK_A && virtualKey < VK_A + 26) {
    const bool shifted = (modifiers & modifier_flags::Shift) != 0;
    const bool caps = (modifiers & modifier_flags::CapsLock) != 0;
    const bool upper = shifted != caps;// CapsLock only flips letter case, unlike Shift
    event.character = static_cast<char>((upper ? 'A' : 'a') + (virtualKey - VK_A));
    return event;
  }

  // Keypad digits and '.' only produce a character with NumLock on; the
  // navigation meaning of the same key never does.
  if (usage >= usage_kbd::Kp1 && usage <= usage_kbd::Kp0) {
    const int digit = (usage == usage_kbd::Kp0) ? 0 : (usage - usage_kbd::Kp1 + 1);
    return (modifiers & modifier_flags::NumLock)
               ? (event.character = static_cast<char>('0' + digit), event)
               : event;
  }
  if (usage == usage_kbd::KpPeriod) {
    if (modifiers & modifier_flags::NumLock) event.character = '.';
    return event;
  }

  // The keypad operators always produce their symbol, NumLock or not.
  switch (usage) {
    case usage_kbd::KpMultiply: event.character = '*'; return event;
    case usage_kbd::KpPlus: event.character = '+'; return event;
    case usage_kbd::KpMinus: event.character = '-'; return event;
    case usage_kbd::KpDivide: event.character = '/'; return event;
    default: break;
  }

  if (usage < 256) {
    const bool shifted = (modifiers & modifier_flags::Shift) != 0;
    event.character = shifted ? kUsageAsciiShifted[usage] : kUsageAsciiUnshifted[usage];
  }
  return event;
}

void SubmitReport(const KDevicePtr<KDevice> &device,
                  const ReportFormat &format, DecoderState &state,
                  kstd::span<const uint8_t> report) {
  using namespace kernel::input;

  if (report.size() < format.reportBytes) return;// never read past the buffer

  switch (format.collectionUsage) {
    case usage_gd::Keyboard: {
      uint16_t current[DecoderState::kMaxPressed]{};
      size_t currentCount = 0;
      uint32_t heldModifiers = 0;

      for (size_t f = 0; f < format.fieldCount; ++f) {
        const ReportField &field = format.fields[f];
        if (field.usagePage != usage_page::Keyboard) continue;
        const int32_t value = ExtractField(field, report);
        const uint16_t usage = field.isArray ? static_cast<uint16_t>(value) : field.usage;
        if (usage == usage_kbd::None) continue;
        if (!field.isArray && value == 0) continue;
        if (ContainsUsage(current, currentCount, usage)) continue;
        // More simultaneous keys than the decoder tracks: drop the
        // overflow deterministically rather than evicting a held key.
        if (currentCount >= DecoderState::kMaxPressed) continue;
        current[currentCount++] = usage;
        heldModifiers |= HeldModifierFlag(usage);
      }

      for (size_t p = 0; p < state.pressedCount; ++p) {
        const uint16_t usage = state.pressed[p];
        if (ContainsUsage(current, currentCount, usage)) continue;
        KE_INPUT_PushEvent(device, MakeKeyEvent(usage, true, heldModifiers | state.lockModifiers));
      }

      for (size_t c = 0; c < currentCount; ++c) {
        const uint16_t usage = current[c];
        if (ContainsUsage(state.pressed, state.pressedCount, usage)) continue;// still held: no repeat make
        state.lockModifiers ^= LockModifierFlag(usage);
        KE_INPUT_PushEvent(device, MakeKeyEvent(usage, false, heldModifiers | state.lockModifiers));
      }

      for (size_t c = 0; c < currentCount; ++c) state.pressed[c] = current[c];
      state.pressedCount = currentCount;
      break;
    }

    case usage_gd::Mouse:
    case usage_gd::Pointer: {
      MousePacket packet;
      for (size_t f = 0; f < format.fieldCount; ++f) {
        const ReportField &field = format.fields[f];
        const int32_t value = ExtractField(field, report);
        if (field.usagePage == usage_page::Button) {
          if (value != 0 && field.usage >= 1 && field.usage <= 5) {
            packet.buttons |= static_cast<uint8_t>(1u << (field.usage - 1));
          }
          continue;
        }
        if (field.usagePage != usage_page::GenericDesktop) continue;
        switch (field.usage) {
          case usage_gd::X:
            packet.dx = value;
            packet.isAbsolute = !field.isRelative;
            break;
          case usage_gd::Y: packet.dy = value; break;
          case usage_gd::Wheel: packet.wheel = static_cast<int8_t>(value); break;
          default: break;
        }
      }
      KE_INPUT_PushEvent(device, packet);
      break;
    }

    case usage_gd::Joystick:
    case usage_gd::Gamepad: {
      GamepadState pad;
      for (size_t f = 0; f < format.fieldCount; ++f) {
        const ReportField &field = format.fields[f];
        const int32_t value = ExtractField(field, report);
        if (field.usagePage == usage_page::Button) {
          if (value != 0 && field.usage >= 1 && field.usage <= 32) {
            pad.buttons |= 1u << (field.usage - 1);
          }
          continue;
        }
        if (field.usagePage != usage_page::GenericDesktop) continue;
        if (field.usage == usage_gd::HatSwitch) {
          DecodeHat(value, field.logicalMin, field.logicalMax, pad.hatX, pad.hatY);
          continue;
        }
        const size_t slot = GamepadAxisSlot(field.usage);
        if (slot < gamepad_axis::Count) {
          pad.axes[slot] = NormalizeAxis(value, field.logicalMin, field.logicalMax);
        }
      }
      KE_INPUT_PushEvent(device, pad);
      break;
    }

    default:
      break;// a collection this layer does not map; silently ignored
  }
}

}// namespace kernel::hid
