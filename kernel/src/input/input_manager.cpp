
#include "input_manager.hpp"

#include "stdkrn.hpp"

#include <Kernel.hpp>
#include <kernel/device.hpp>
#include <kernel/input.hpp>
#include <kernel/kobject/KDirectoryObject.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/spinlock.hpp>
#include <kernel/time.hpp>

#include "event_ring.hpp"

// Passive-level event multiplexer. No ISR may reach this translation unit:
// every input driver buffers raw hardware data in its interrupt handler and
// calls KE_INPUT_PushEvent from a worker thread, which is what makes it safe
// for the functions below to take a lock and walk lookup tables. Decoding --
// scancodes, packet framing, HID report descriptors -- lives entirely in the
// drivers; nothing here knows about any particular hardware, and nothing here
// knows about the screen either, so no coordinate is ever clamped or scaled.
namespace {

// Guards the event ring, the device->unit table and the gamepad snapshot
// store. A spinlock rather than a kernel::Mutex: the critical sections are
// a handful of index writes, blocking would let a producer be descheduled
// while holding the queue, and ResetIfEmpty already runs with interrupts
// disabled around the dispatcher header, which lock_irqsave() composes with
// and a mutex does not.
kernel::SpinLock g_eventLock;
kernel::input::EventRing g_eventRing;

// Manual-reset: signalled while the event ring is non-empty. Readers reset
// it with interrupts off only after seeing the ring empty, so a push that
// lands in between cannot be lost (single CPU).
kernel::KDispatcherHeader g_eventHeader;
kernel::KObjectPtr<kernel::KIOObject> g_inputStream;

uint32_t g_modifiers = 0;

// Fixed-size device -> unitId table. The raw KDevice* is an identity key
// only and is never dereferenced, so the push path does no refcount work.
// Input devices are created once at boot and never torn down; a future
// hot-unplug path (USB) must release the slot on device removal.
constexpr size_t kMaxInputDevices = 16;
struct UnitSlot {
  const kernel::KDevice *device = nullptr;
  uint8_t previousButtons = 0;
};
UnitSlot g_units[kMaxInputDevices];

// Latest-state storage for gamepads/joysticks. A pad reporting continuous
// axis state at 60 Hz would otherwise flood a ring shared with the keyboard,
// so Gamepad reports overwrite a slot here and never touch the ring at all.
constexpr size_t kMaxGamepads = 8;
struct GamepadSlot {
  const kernel::KDevice *device = nullptr;
  kernel::input::GamepadState state{};
};
GamepadSlot g_gamepads[kMaxGamepads];

// Finds or claims this device's unit slot. Bounded linear scan over a fixed
// table, allocation-free. Returns nullptr when the table is full.
UnitSlot *FindOrClaimUnit(const kernel::KDevice *device, uint16_t &unitId) noexcept {
  for (size_t i = 0; i < kMaxInputDevices; ++i) {
    if (g_units[i].device == device) {
      unitId = static_cast<uint16_t>(i);
      return &g_units[i];
    }
  }
  for (size_t i = 0; i < kMaxInputDevices; ++i) {
    if (g_units[i].device != nullptr) continue;
    g_units[i].device = device;
    g_units[i].previousButtons = 0;
    unitId = static_cast<uint16_t>(i);
    return &g_units[i];
  }
  return nullptr;
}

// Builds one MouseInputEvent from a normalized pointer report: diffs button
// state into mouse_button_flags::*Down/*Up across all 5 buttons and OR-in
// Wheel with the signed delta when the report carries one. Coordinates pass
// through untouched -- relative deltas as-is, absolute coordinates unscaled
// with isMovementAbsolute set. Cursor position, screen confinement and any
// absolute->screen mapping belong to the GFX/USER layer that owns the screen
// rectangle (kernel/src/user/rawinput.cpp).
kernel::input::MouseInputEvent BuildMouseEvent(const kernel::input::MousePacket &packet,
                                               UnitSlot &unit) noexcept {
  using namespace kernel::input;

  MouseInputEvent event;
  event.isMovementAbsolute = packet.isAbsolute;
  event.lastX = packet.dx;
  event.lastY = packet.dy;

  const auto diffButton = [&](uint8_t bit, uint16_t downFlag, uint16_t upFlag) {
    const bool wasDown = (unit.previousButtons & bit) != 0;
    const bool isDown = (packet.buttons & bit) != 0;
    if (isDown && !wasDown) event.buttonFlags |= downFlag;
    else if (!isDown && wasDown)
      event.buttonFlags |= upFlag;
  };
  diffButton(mouse_buttons::Left, mouse_button_flags::LeftDown, mouse_button_flags::LeftUp);
  diffButton(mouse_buttons::Right, mouse_button_flags::RightDown, mouse_button_flags::RightUp);
  diffButton(mouse_buttons::Middle, mouse_button_flags::MiddleDown, mouse_button_flags::MiddleUp);
  diffButton(mouse_buttons::Button4, mouse_button_flags::Button4Down, mouse_button_flags::Button4Up);
  diffButton(mouse_buttons::Button5, mouse_button_flags::Button5Down, mouse_button_flags::Button5Up);
  unit.previousButtons = packet.buttons;

  if (packet.wheel != 0) {
    event.buttonFlags |= mouse_button_flags::Wheel;
    event.buttonData = packet.wheel;
  }

  return event;
}

// Resets the header if the ring is (still) empty. Returns true if empty.
bool ResetIfEmpty() noexcept {
  const uintptr_t irq_state = g_eventLock.lock_irqsave();
  const bool empty = g_eventRing.Empty();
  if (empty) g_eventHeader.Reset();
  g_eventLock.unlock_irqrestore(irq_state);
  return empty;
}

// \Device\Input: each Read returns whole AnyInputEvent records and blocks
// until at least one is available.
kernel::IoStatus InputRead(kernel::KIOObject &, kstd::span<uint8_t> buffer, size_t &transferred) {
  using kernel::input::AnyInputEvent;
  transferred = 0;
  if (buffer.size() < sizeof(AnyInputEvent)) return kernel::IoStatus::InvalidSize;
  for (;;) {
    AnyInputEvent event;
    while (transferred + sizeof(AnyInputEvent) <= buffer.size() && KE_INPUT_DequeueEvent(event)) {
      __builtin_memcpy(buffer.data() + transferred, &event, sizeof(event));
      transferred += sizeof(event);
    }
    if (transferred) return kernel::IoStatus::Success;
    if (ResetIfEmpty()) {
      (void) KE_SCHED_WaitForHeader(g_eventHeader, kernel::WAIT_INFINITE);
    }
  }
}

kernel::IoStatus InputClose(kernel::KIOObject &) { return kernel::IoStatus::Success; }

const kernel::KIOOps InputOps{InputRead, {}, {}, InputClose};

void RegisterInputStream() noexcept {
  const auto dir = KE_OB_LookupObject("\\Device").As<kernel::KDirectoryObject>();
  auto stream = kernel::CreateKObject<kernel::KIOObject>("Input");
  if (!dir || !stream) {
    KE_Print("Input manager: cannot register \\Device\\Input");
    return;
  }
  stream->ops = &InputOps;
  stream->mode = kernel::KIOObject::OpenMode::Read;
  stream->waitable = &g_eventHeader;
  if (KE_OB_InsertObject(dir, kernel::KObjectPtr<kernel::KObject>(stream.get()))) {
    g_inputStream = stream;
  }
}

}// namespace

namespace kernel::input {
void init() {
  g_eventRing = EventRing();
  for (auto &unit: g_units) unit = UnitSlot{};
  for (auto &pad: g_gamepads) pad = GamepadSlot{};
  g_modifiers = 0;
  g_eventHeader = kernel::KDispatcherHeader();
  RegisterInputStream();
}
}// namespace kernel::input

UNDOS_KERNEL_API_DEF void KE_INPUT_Initialize() noexcept {
  kernel::input::init();
}

// PASSIVE LEVEL ONLY -- see the contract in kernel/input.hpp. Routing is
// decided here, by report kind, so a driver cannot accidentally push axis
// state onto the event ring. There is deliberately no silent default: an
// unknown kind is ignored without touching either the ring or the snapshot
// store, but every known kind is handled explicitly.
UNDOS_KERNEL_API_DEF void KE_INPUT_PushEvent(const kernel::KDevicePtr<kernel::KDevice> &device,
                                             const kernel::input::KInputReport &report) noexcept {
  using namespace kernel::input;

  if (!device) return;
  const kernel::KDevice *key = device.get();

  AnyInputEvent event;
  bool queued = false;

  const uintptr_t irq_state = g_eventLock.lock_irqsave();
  switch (report.kind) {
    case InputReportKind::Key: {
      uint16_t unitId = 0;
      if (FindOrClaimUnit(key, unitId) != nullptr) {
        event.keyboard = report.key;
        event.keyboard.unitId = unitId;
        g_modifiers = report.key.modifiers;
        queued = true;
      }
      break;
    }
    case InputReportKind::Pointer: {
      uint16_t unitId = 0;
      if (UnitSlot *unit = FindOrClaimUnit(key, unitId)) {
        event.mouse = BuildMouseEvent(report.pointer, *unit);
        event.mouse.unitId = unitId;
        queued = true;
      }
      break;
    }
    case InputReportKind::Gamepad: {
      GamepadSlot *slot = nullptr;
      for (auto &candidate: g_gamepads) {
        if (candidate.device == key) {
          slot = &candidate;
          break;
        }
      }
      if (slot == nullptr) {
        for (auto &candidate: g_gamepads) {
          if (candidate.device != nullptr) continue;
          candidate.device = key;
          slot = &candidate;
          break;
        }
      }
      // All slots taken: drop the report rather than evict another pad.
      if (slot != nullptr) {
        const uint32_t sequence = slot->state.sequence + 1;
        slot->state = report.gamepad;
        slot->state.sequence = sequence;
      }
      break;
    }
  }

  if (queued) {
    event.base.timestamp = KE_TIME_GetSystemTime();
    g_eventRing.Push(event);
  }
  g_eventLock.unlock_irqrestore(irq_state);

  if (queued) KE_EVENT_Signal(g_eventHeader, true);
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_GetGamepadState(const kernel::KDevicePtr<kernel::KDevice> &device,
                                                   kernel::input::GamepadState &out) noexcept {
  if (!device) return false;
  const kernel::KDevice *key = device.get();

  bool found = false;
  const uintptr_t irq_state = g_eventLock.lock_irqsave();
  for (const auto &candidate: g_gamepads) {
    if (candidate.device != key) continue;
    out = candidate.state;
    found = true;
    break;
  }
  g_eventLock.unlock_irqrestore(irq_state);
  return found;
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_DequeueEvent(kernel::input::AnyInputEvent &out) noexcept {
  const uintptr_t irq_state = g_eventLock.lock_irqsave();
  const bool popped = g_eventRing.Pop(out);
  g_eventLock.unlock_irqrestore(irq_state);
  return popped;
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_WaitEvent(kernel::input::AnyInputEvent &out,
                                             kstd::chrono::milliseconds timeout) noexcept {
  for (;;) {
    if (KE_INPUT_DequeueEvent(out)) return true;
    if (!ResetIfEmpty()) continue;
    if (KE_SCHED_WaitForHeader(g_eventHeader, timeout) != kernel::WaitStatus::Signalled) return false;
  }
}

// Reflects the modifier bitmask carried by the most recently pushed keyboard
// report (kernel::input::modifier_flags::*).
UNDOS_KERNEL_API_DEF void KE_INPUT_GetModifierState(uint32_t &modifiers) noexcept {
  modifiers = g_modifiers;
}
