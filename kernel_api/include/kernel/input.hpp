
#pragma once

#include <chrono.hpp>
#include <string_view.hpp>

#include <kernel/StaticIdentifier.hpp>
#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>

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

  uint16_t virtualKey; // VK_* from scancode_tables.hpp
  uint8_t scanCode;    // raw set-1 scancode (no E0 prefix / break bit)
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

  bool isMovementAbsolute;// always false for PS/2; reserved for a future absolute pointer device
  int32_t lastX, lastY;   // relative dx/dy for this packet (screen-space; dy already sign-fixed)
  uint16_t buttonFlags;   // mouse_button_flags::* transitions that occurred in this packet
  int16_t buttonData;     // signed wheel delta; valid only when buttonFlags has Wheel set

  constexpr explicit MouseInputEvent(uint16_t unit = 0) noexcept
      : InputEvent(TYPE_ID, unit), isMovementAbsolute(false), lastX(0), lastY(0), buttonFlags(0), buttonData(0) {}
};

// Mouse packet framing negotiated with the aux device (see
// I8042_NegotiateMouseExtensions in drivers/input_i8042/i8042.cpp and the
// MousePacketDecoder in kernel/src/input/mouse_packet.hpp). Standard3Byte is
// always the safe default; upgraded only after the device confirms a
// recognised device ID for the corresponding knock sequence.
enum class MousePacketMode : uint8_t {
  Standard3Byte,
  IntelliMouse4Byte,
  IntelliMouseExplorer4Byte,
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

using ConsumerFn = cfunc<void(const InputEvent &)>;

}// namespace kernel::input

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_Initialize
 *
 * Initializes the kernel input manager's raw byte rings. Must be called
 * once at boot before any driver reports raw bytes.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_Initialize);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_ReportKeyboardData
 *
 * Pushes one raw byte read from the keyboard (main PS/2) port into the
 * keyboard byte ring. ISR-safe: does only a lock-free ring push, no
 * allocation, no locking.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_ReportKeyboardData, uint8_t data);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_ReportAuxData
 *
 * Pushes one raw byte read from the aux (mouse) PS/2 port into the aux byte
 * ring. ISR-safe: does only a lock-free ring push, no allocation, no
 * locking.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_ReportAuxData, uint8_t data);

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
 */
UNDOS_KERNEL_PUBLIC_V1API(bool, KE_INPUT_WaitEvent, kernel::input::AnyInputEvent &out, kstd::chrono::milliseconds timeout);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_SetConsumer
 *
 * Registers a callback invoked synchronously on the input pump thread for
 * every decoded event (the seam USER attaches to in Step 3 of the parent
 * plan). Populated starting with the decoder-pump task (Step 7).
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_SetConsumer, kernel::input::ConsumerFn consumer);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_GetModifierState
 *
 * Returns the current Shift/Ctrl/Alt/Caps/Num/Scroll modifier bitmask.
 * Populated starting with the keyboard translation task (Step 5).
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_GetModifierState, uint32_t &modifiers);

/**
 * @ingroup INPUT
 * @brief Method KE_INPUT_SetMousePacketMode
 *
 * Switches the aux byte stream's packet framing (3-byte standard vs. 4-byte
 * IntelliMouse/IntelliMouse-Explorer). Called by the i8042 driver once it
 * has confirmed the corresponding device ID via the sample-rate knock
 * sequence (Task 15); a partial packet in flight is discarded rather than
 * misparsed. Defaults to Standard3Byte.
 */
UNDOS_KERNEL_PUBLIC_V1API(void, KE_INPUT_SetMousePacketMode, kernel::input::MousePacketMode mode);
