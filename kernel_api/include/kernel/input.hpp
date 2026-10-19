
#pragma once

#include <chrono.hpp>
#include <expected.hpp>
#include <string_view.hpp>

#include <kernel/StaticIdentifier.hpp>
#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/kerror.hpp>

// Windows2000-style, RTTI-less input event hierarchy: a non-virtual
// InputEvent base carries a StaticIdentifier-tagged type field (mirrors
// DeviceType in device.hpp / ObjectType in kobject/ObjectType.hpp) so
// KeyboardInputEvent/MouseInputEvent can be downcast via is<T>()/as<T>()
// without RTTI or a vtable, and can live in a fixed-size union (see
// AnyInputEvent) for the event ring added in a later Step 1 task.
namespace kernel::input {

struct InputEventTypeTag {
  // Unique seed for InputEventType to minimize hash collisions across
  // subsystems. Deliberately distinct from DeviceTypeTag::seed (device.hpp)
  // and ObjectTypeTag::seed (kobject/ObjectType.hpp).
  static constexpr uint64_t seed = 0x9b3d2f6a41c7e850ull;
};
using InputEventType = kernel::StaticIdentifier<InputEventTypeTag>;

consteval InputEventType operator""_evt(const char *s, size_t n) {
  return InputEventType::from_string(kstd::string_view{s, n});
}

// Base Input Event header, standard across all device types.
struct alignas(8) InputEvent {
  InputEventType type;                 // Type identifier hash (e.g. "mouse"_evt)
  uint16_t unitId;                     // Device index (Keyboard0 -> 0, Mouse0 -> 0, ...)
  uint16_t flags;                      // Global event/delivery flags (reserved for USER, Step 3)
  kstd::chrono::nanoseconds timestamp; // KE_TIME_GetSystemTime() at decode

  constexpr InputEvent(InputEventType t, uint16_t unit = 0) noexcept
      : type(t), unitId(unit), flags(0), timestamp(kstd::chrono::nanoseconds{0}) {}

  // Static downcasting without RTTI.
  template<typename Derived>
  [[nodiscard]] bool is() const noexcept { return type == Derived::TYPE_ID; }

  template<typename Derived>
  [[nodiscard]] const Derived *as() const noexcept {
    return is<Derived>() ? static_cast<const Derived *>(this) : nullptr;
  }

  template<typename Derived>
  [[nodiscard]] Derived *as() noexcept {
    return is<Derived>() ? static_cast<Derived *>(this) : nullptr;
  }
};

// Keyboard make/break flags, carried in KeyboardInputEvent::keyFlags.
namespace key_flags {
constexpr uint16_t Break = 1 << 0;   // set = key up, clear = key down
constexpr uint16_t Extended = 1 << 1;// E0-prefixed scancode
constexpr uint16_t Repeat = 1 << 2;  // typematic repeat of an already-down key
}// namespace key_flags

// Shift/Ctrl/Alt/Caps/Num/Scroll state, carried in KeyboardInputEvent::modifiers
// and returned by KE_INPUT_GetModifierState. Left/Right variants are tracked
// individually; a key that only cares about either side can OR the pair.
namespace modifier_flags {
constexpr uint32_t LShift = 1 << 0;
constexpr uint32_t RShift = 1 << 1;
constexpr uint32_t LControl = 1 << 2;
constexpr uint32_t RControl = 1 << 3;
constexpr uint32_t LAlt = 1 << 4;
constexpr uint32_t RAlt = 1 << 5;
constexpr uint32_t CapsLock = 1 << 6; // toggled, not held
constexpr uint32_t NumLock = 1 << 7;  // toggled, not held
constexpr uint32_t ScrollLock = 1 << 8;// toggled, not held

constexpr uint32_t Shift = LShift | RShift;
constexpr uint32_t Control = LControl | RControl;
constexpr uint32_t Alt = LAlt | RAlt;
}// namespace modifier_flags

// Keyboard Input Event packet.
struct alignas(8) KeyboardInputEvent : public InputEvent {
  static constexpr auto TYPE_ID = "keyboard"_evt;

  uint16_t virtualKey; // VK_*, derived by drivers/input_hid/ from the HID usage
  uint8_t scanCode;    // HID Keyboard/Keypad (page 0x07) usage ID, truncated to 8 bits
  uint16_t keyFlags;   // key_flags::*
  char character;      // 0 when not printable
  uint32_t modifiers;  // Shift/Ctrl/Alt/Caps/Num/Scroll bitmask at decode (KE_INPUT_GetModifierState)

  constexpr explicit KeyboardInputEvent(uint16_t unit = 0) noexcept
      : InputEvent(TYPE_ID, unit), virtualKey(0), scanCode(0), keyFlags(0), character(0), modifiers(0) {}
};

// Mouse button/wheel transition bits, carried in MouseInputEvent::buttonFlags
// (mirrors Win32 RAWMOUSE).
namespace mouse_button_flags {
constexpr uint16_t LeftDown = 1 << 0;
constexpr uint16_t LeftUp = 1 << 1;
constexpr uint16_t RightDown = 1 << 2;
constexpr uint16_t RightUp = 1 << 3;
constexpr uint16_t MiddleDown = 1 << 4;
constexpr uint16_t MiddleUp = 1 << 5;
constexpr uint16_t Button4Down = 1 << 6;// IntelliMouse Explorer only
constexpr uint16_t Button4Up = 1 << 7;
constexpr uint16_t Button5Down = 1 << 8;// IntelliMouse Explorer only
constexpr uint16_t Button5Up = 1 << 9;
constexpr uint16_t Wheel = 1 << 10;// buttonData holds a signed wheel delta
}// namespace mouse_button_flags

// Mouse Input Event packet (multi-axis, multi-button).
struct alignas(8) MouseInputEvent : public InputEvent {
  static constexpr auto TYPE_ID = "mouse"_evt;

  // Set when lastX/lastY carry the device's own logical absolute coordinate
  // (tablet/touchscreen/HID digitizer) rather than a relative delta. The
  // input manager never scales or clamps either form -- mapping into a
  // screen rectangle belongs to the GFX/USER layer that owns it.
  bool isMovementAbsolute;
  int32_t lastX, lastY;   // reported dx/dy (relative) or logical coordinate (absolute)
  uint16_t buttonFlags;   // mouse_button_flags::* transitions that occurred in this packet
  int16_t buttonData;     // signed wheel delta; valid only when buttonFlags has Wheel set

  constexpr explicit MouseInputEvent(uint16_t unit = 0) noexcept
      : InputEvent(TYPE_ID, unit), isMovementAbsolute(false), lastX(0), lastY(0), buttonFlags(0), buttonData(0) {}
};

// Normalized button bitmask, reported by any pointer driver.
namespace mouse_buttons {
constexpr uint8_t Left = 1 << 0;
constexpr uint8_t Right = 1 << 1;
constexpr uint8_t Middle = 1 << 2;
constexpr uint8_t Button4 = 1 << 3;
constexpr uint8_t Button5 = 1 << 4;
}// namespace mouse_buttons

// One normalized pointer report. Relative by default; `isAbsolute` selects
// coordinate semantics so tablets/touchscreens/HID digitizers report
// natively instead of faking deltas.
struct MousePacket {
  // Relative: delta since the last report. Absolute: the device's own
  // logical coordinate, NOT screen pixels -- the manager neither scales
  // nor clamps these; mapping into a screen rectangle is GFX/USER's job.
  int32_t dx = 0;
  int32_t dy = 0;     // positive = downward
  int8_t wheel = 0;
  uint8_t buttons = 0;// mouse_buttons::*
  bool isAbsolute = false;
};

// Normalized gamepad/joystick axes, named after their HID Generic Desktop
// usages (X, Y, Rx, Ry, Z, Rz).
namespace gamepad_axis {
constexpr size_t LeftX = 0;
constexpr size_t LeftY = 1;
constexpr size_t RightX = 2;
constexpr size_t RightY = 3;
constexpr size_t LeftTrigger = 4;
constexpr size_t RightTrigger = 5;
constexpr size_t Count = 6;
}// namespace gamepad_axis

// Normalized gamepad/joystick snapshot. Axes are scaled to a fixed signed
// range so an 8-bit USB HID pad and a timed analog gameport read are
// directly comparable; unused axes stay 0 and unsupported buttons stay
// clear. `sequence` is stamped by the manager on every push so a consumer
// can tell it skipped samples.
struct GamepadState {
  static constexpr int16_t kAxisMin = -32767;
  static constexpr int16_t kAxisMax = 32767;

  int16_t axes[gamepad_axis::Count] = {};// normalized, centred on 0
  uint32_t buttons = 0;                  // bit n = button n down
  int8_t hatX = 0, hatY = 0;             // -1/0/+1 d-pad or POV hat
  uint32_t sequence = 0;                 // stamped by the manager
};

// Fixed-size container for the event ring (added in a later Step 1 task):
// holds either concrete event without RTTI/virtuals. All three members are
// non-virtual, trivially copyable and share InputEvent as their initial
// subobject, so reading `.base` after writing `.keyboard`/`.mouse` (or vice
// versa) is well-defined for the InputEvent-owned members.
union AnyInputEvent {
  InputEvent base;
  KeyboardInputEvent keyboard;
  MouseInputEvent mouse;

  constexpr AnyInputEvent() noexcept : base(InputEventType{0}) {}
};

// Which member of KInputReport is live.
enum class InputReportKind : uint8_t {
  Key = 0,    // one key transition, already translated to a virtual key
  Pointer = 1,// one normalized pointer report, relative or absolute
  Gamepad = 2,// full latest axis/button state of one pad
};

// The single record every input driver pushes. Adding a new input class
// costs a tag plus a union member, not a new exported function. All three
// members are trivially copyable PODs; the converting constructors are the
// only way to build one, so the tag can never disagree with the payload.
struct KInputReport {
  InputReportKind kind = InputReportKind::Key;
  union {
    KeyboardInputEvent key;
    MousePacket pointer;
    GamepadState gamepad;
  };

  constexpr KInputReport(const KeyboardInputEvent &e) noexcept : kind(InputReportKind::Key), key(e) {}
  constexpr KInputReport(const MousePacket &p) noexcept : kind(InputReportKind::Pointer), pointer(p) {}
  constexpr KInputReport(const GamepadState &g) noexcept : kind(InputReportKind::Gamepad), gamepad(g) {}
};

using GamepadStateResult = kstd::expected<GamepadState, KernelError>;
using InputEventResult = kstd::expected<AnyInputEvent, KernelError>;

}// namespace kernel::input

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_Initialize
 *
 * Initializes the kernel input manager's event ring, gamepad snapshot store
 * and \Device\Input stream. Must be called once at boot before any driver
 * pushes a report. The ring is usable even when publishing \Device\Input
 * fails; that failure is returned.
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_INPUT_Initialize);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_PushEvent
 *
 * The single input ingestion point. Every code it carries is a HID usage ID
 * or derived from one, and drivers/input_hid/ is its intended sole caller:
 * a transport's job stops at delivering HID report bytes.
 *
 * CONTEXT: PASSIVE LEVEL ONLY -- must not be called from an interrupt
 * handler. Drivers buffer raw hardware data in their ISR and call this from
 * a worker thread (see drivers/input_i8042/ps2_pump.cpp for the reference
 * implementation). It takes an irqsave spinlock, walks small lookup tables
 * and signals a dispatcher object, none of which belongs in an ISR.
 *
 * `device` is the reporting device and becomes the event's source identity;
 * the manager derives the emitted event's unitId from it. Routing is by
 * `report.kind`:
 *   Key / Pointer -> normalized, stamped, queued on the event ring and
 *                    \Device\Input signalled.
 *   Gamepad       -> overwrites that device's snapshot slot and stamps
 *                    `sequence`; the event ring is NOT touched.
 *
 * Errors: InvalidArgument (null device or unknown report kind),
 * ResourceLimitExceeded (device/gamepad table full; the report is dropped).
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError, KE_INPUT_PushEvent,
                          const kernel::KDevicePtr<kernel::KDevice> &device,
                          const kernel::input::KInputReport &report);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_GetGamepadState
 *
 * Returns the latest snapshot pushed by `device`. Errors: InvalidArgument
 * (null device), NotFound (the device never pushed a Gamepad report).
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::input::GamepadStateResult, KE_INPUT_GetGamepadState,
                          const kernel::KDevicePtr<kernel::KDevice> &device);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_DequeueEvent
 *
 * Pops the oldest decoded event from the event ring. Returns false if the
 * ring is empty. Populated starting with the decoder-pump task (Step 7).
 */
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_INPUT_DequeueEvent, kernel::input::AnyInputEvent &out);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_WaitEvent
 *
 * Kernel-side consumer (USER raw input thread): pops the oldest event,
 * blocking up to `timeout` (WAIT_INFINITE allowed) until one arrives.
 * Fails with Timeout when nothing arrived, or with the scheduler's wait error.
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::input::InputEventResult, KE_INPUT_WaitEvent, kstd::chrono::milliseconds timeout);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_GetModifierState
 *
 * Returns the current Shift/Ctrl/Alt/Caps/Num/Scroll modifier bitmask, taken
 * from the most recently pushed keyboard report.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_GetModifierState, uint32_t &modifiers);
