
#include "input_manager.hpp"

#include <Kernel.hpp>
#include <kernel/input.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

#include <mutex.hpp>

#include "event_ring.hpp"
#include "key_translate.hpp"
#include "mouse_packet.hpp"

namespace {

// Compile-time trace of decoded keyboard/mouse events, printed from the
// pump thread as each event is queued. Gated per the plan's Step 7 design
// (removed/flipped false once USER, Step 3 of the parent plan, is the real
// consumer instead of serial).
constexpr bool kInputTrace = true;

// Single-producer/single-consumer byte ring. The producer (an ISR) only
// ever writes `head`; the consumer (the input pump thread) only ever writes
// `tail`. No locks: kernel::SpinLock is documented as not ISR-safe
// (kernel/spinlock.hpp), and this ring must be safe to push from interrupt
// context.
class ByteRing {
  public:
  static constexpr size_t kCapacity = 256;

  // ISR-safe: only touches `head` (and reads `tail`), never blocks.
  bool Push(uint8_t byte) noexcept {
    const size_t head = m_head;
    const size_t nextHead = (head + 1) % kCapacity;
    if (nextHead == m_tail) {
      // Ring full: drop the byte rather than overwrite unread data or spin.
      return false;
    }
    m_buffer[head] = byte;
    m_head = nextHead;
    return true;
  }

  // Consumer-side only: touches `tail` (and reads `head`).
  bool Pop(uint8_t &out) noexcept {
    const size_t tail = m_tail;
    if (tail == m_head) {
      return false;
    }
    out = m_buffer[tail];
    m_tail = (tail + 1) % kCapacity;
    return true;
  }

  private:
  volatile size_t m_head = 0;
  volatile size_t m_tail = 0;
  uint8_t m_buffer[kCapacity]{};
};

ByteRing g_keyboardRing;
ByteRing g_auxRing;

// Fed exclusively from the pump thread (InputPumpThread below); never
// touched from ISR context.
kernel::input::KeyTranslator g_keyTranslator;
kernel::input::MousePacketDecoder g_mouseDecoder;

int32_t g_mouseX = 0;
int32_t g_mouseY = 0;
uint8_t g_prevMouseButtons = 0;

// Bounded event queue + the thread that owns pushing into it. Guarded by a
// kernel::Mutex (not a ByteRing-style lock-free ring): unlike the byte
// rings above, this queue is popped from an ordinary thread context
// (KE_INPUT_DequeueEvent), never from an ISR, so a real lock is both safe
// and necessary.
kernel::Mutex g_eventRingLock;
kernel::input::EventRing g_eventRing;
kernel::KObjectPtr<kernel::KThreadObject> g_pumpThread;

kernel::input::ConsumerFn g_consumer{nullptr};
uint32_t g_modifiers = 0;

int32_t ClampAxis(int32_t value, int32_t maxExclusive) noexcept {
  if (value < 0) return 0;
  if (value >= maxExclusive) return maxExclusive - 1;
  return value;
}

// Builds one MouseInputEvent from a single decoded aux packet (Task 16):
// applies the scale factor and screen-edge clamp to the accumulated
// position, reporting only the delta actually applied as lastX/lastY, diffs
// button state into mouse_button_flags::*Down/*Up across all 5 buttons, and
// OR-in Wheel with the signed delta when the packet carries one. Exactly
// one event per packet, never split.
kernel::input::MouseInputEvent BuildMouseEvent(const kernel::input::MousePacket &packet) noexcept {
  using namespace kernel::input;

  const int32_t proposedX = g_mouseX + packet.dx;
  const int32_t proposedY = g_mouseY + packet.dy;
  const int32_t clampedX = ClampAxis(proposedX, 1600); // TODO Check the associated virtual desktop size
  const int32_t clampedY = ClampAxis(proposedY, 1200);

  MouseInputEvent event;
  event.lastX = clampedX - g_mouseX;
  event.lastY = clampedY - g_mouseY;
  g_mouseX = clampedX;
  g_mouseY = clampedY;

  const auto diffButton = [&](uint8_t bit, uint16_t downFlag, uint16_t upFlag) {
    const bool wasDown = (g_prevMouseButtons & bit) != 0;
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
  g_prevMouseButtons = packet.buttons;

  if (packet.wheel != 0) {
    event.buttonFlags |= mouse_button_flags::Wheel;
    event.buttonData = packet.wheel;
  }

  return event;
}

// Common tail for both decoded event kinds: stamps the timestamp, queues
// the event (subject to EventRing's drop policy) and, when registered,
// invokes the consumer synchronously -- this is the seam USER attaches to
// in Step 3 of the parent plan. `kInputTrace` prints a human-readable line
// via is<T>()/as<T>() downcasting, with mouse motion traces rate-limited so
// continuous movement does not flood the serial log.
void QueueEvent(kernel::input::AnyInputEvent &event) noexcept {
  using namespace kernel::input;

  event.base.timestamp = KE_TIME_GetSystemTime();

  {
    kstd::unique_lock guard(g_eventRingLock);
    g_eventRing.Push(event);
  }

  if (g_consumer) {
    g_consumer(event.base);
  }

  if constexpr (kInputTrace) {
    if (const auto *key = event.base.as<KeyboardInputEvent>()) {
      early_print_fmt("INPUT: Key vk=0x{x} sc=0x{x} ch='{}' flags=0x{x} mods=0x{x} t={}\r\n",
                      key->virtualKey, key->scanCode, key->character, key->keyFlags,
                      key->modifiers, key->timestamp.count());
    } else if (const auto *mouse = event.base.as<MouseInputEvent>()) {
      // Rate-limit pure-motion traces (one in every N) so a continuously
      // moving mouse cannot starve the boot log; clicks/wheel are never
      // rate-limited since they are comparatively rare and always useful.
      static uint32_t s_motionTraceCounter = 0;
      constexpr uint32_t kMotionTraceEvery = 8;
      const bool isPureMotion = mouse->buttonFlags == 0;
      if (!isPureMotion || (++s_motionTraceCounter % kMotionTraceEvery) == 0) {
        early_print_fmt("INPUT: Mouse dx={} dy={} btnFlags=0x{x} wheel={} t={}\r\n",
                        mouse->lastX, mouse->lastY, mouse->buttonFlags, mouse->buttonData,
                        mouse->timestamp.count());
      }
    }
  }
}

// Drains both byte rings outside interrupt context, feeding the decoders
// and queueing whatever events they produce (Task 16). Blocks (rather than
// spinning) once both rings are empty; KE_INPUT_ReportKeyboardData/
// ReportAuxData unblock it again as soon as a new byte arrives.
void InputPumpThread(void *) {
  using namespace kernel::input;

  for (;;) {
    bool didWork = false;
    uint8_t byte = 0;

    while (g_keyboardRing.Pop(byte)) {
      didWork = true;
      KeyboardInputEvent keyEvent;
      if (g_keyTranslator.Feed(byte, keyEvent)) {
        g_modifiers = keyEvent.modifiers;
        AnyInputEvent event;
        event.keyboard = keyEvent;
        QueueEvent(event);
      }
    }

    while (g_auxRing.Pop(byte)) {
      didWork = true;
      MousePacket packet;
      if (g_mouseDecoder.Feed(byte, packet)) {
        AnyInputEvent event;
        event.mouse = BuildMouseEvent(packet);
        QueueEvent(event);
      }
    }

    if (!didWork) {
      KE_SCHED_Block();
    }
  }
}

}// namespace

namespace kernel::input {
void init() {
  g_keyboardRing = ByteRing();
  g_auxRing = ByteRing();
  g_keyTranslator = KeyTranslator();
  g_mouseDecoder = MousePacketDecoder();
  g_eventRing = EventRing();
  g_mouseX = 0;
  g_mouseY = 0;
  g_prevMouseButtons = 0;
  g_consumer = nullptr;
  g_modifiers = 0;

  g_pumpThread = KE_SCHED_CreateThread(&InputPumpThread, nullptr, 0);

  early_print_fmt("Input manager initialized.\r\n");
}
}// namespace kernel::input

UNDOS_KERNEL_API_DEF void KE_INPUT_Initialize() noexcept {
  kernel::input::init();
}

UNDOS_KERNEL_API_DEF void KE_INPUT_ReportKeyboardData(uint8_t data) noexcept {
  g_keyboardRing.Push(data);
  if (g_pumpThread) {
    KE_SCHED_Unblock(g_pumpThread);
  }
}

UNDOS_KERNEL_API_DEF void KE_INPUT_ReportAuxData(uint8_t data) noexcept {
  g_auxRing.Push(data);
  if (g_pumpThread) {
    KE_SCHED_Unblock(g_pumpThread);
  }
}

UNDOS_KERNEL_API_DEF bool KE_INPUT_DequeueEvent(kernel::input::AnyInputEvent &out) noexcept {
  kstd::unique_lock guard(g_eventRingLock);
  return g_eventRing.Pop(out);
}

UNDOS_KERNEL_API_DEF void KE_INPUT_SetConsumer(kernel::input::ConsumerFn consumer) noexcept {
  g_consumer = consumer;
}

// Reflects the last modifier bitmask stamped onto a decoded KeyboardInputEvent
// by g_keyTranslator (kernel::input::modifier_flags::*).
UNDOS_KERNEL_API_DEF void KE_INPUT_GetModifierState(uint32_t &modifiers) noexcept {
  modifiers = g_modifiers;
}

UNDOS_KERNEL_API_DEF void KE_INPUT_SetMousePacketMode(kernel::input::MousePacketMode mode) noexcept {
  g_mouseDecoder.SetMode(mode);
}
