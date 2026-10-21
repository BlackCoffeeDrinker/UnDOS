#pragma once

#include <Kernel.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>

#include "hid_report.hpp"
#include "i8042_registers.hpp"
#include "ps2_byte_ring.hpp"
#include "ps2_hid_reports.hpp"
#include "ps2_key_translate.hpp"
#include "ps2_mouse_packet.hpp"

// Device and controller state shared between the capture half of the
// driver (i8042.cpp's ISRs) and the decoding half (ps2_pump.cpp).

// SHARING CONTRACT: the ISR writes the ByteRing and the drop counter and
// nothing else; the pump thread owns every other field. Keep the ring the
// only ISR-reachable member -- a future LED/Set_Report path must not let
// the ISR touch decoder state.
struct KeyboardDeviceContext {
  ps2::ByteRing bytes;               // ISR producer, pump consumer
  uint32_t droppedBytes = 0;         // ring-full counter, observability
  ps2::KeyTranslator translator;     // scancode-set-1 -> HID usage
  ps2::KeyboardReportBuilder builder;// held-usage set -> HID report bytes
  kernel::hid::ReportFormat format;  // parsed once, at attach time
  kernel::hid::DecoderState decoder; // previous report, for make/break diffing
};

struct PointerDeviceContext {
  ps2::ByteRing bytes;// ISR producer, pump consumer
  uint32_t droppedBytes = 0;
  ps2::MousePacketDecoder decoder; // PS/2 3/4-byte framing
  kernel::hid::ReportFormat format;// parsed once, at attach time
  kernel::hid::DecoderState hidState;
};

// Per-controller state, held in the FDO's deviceExtension (mirrors
// drivers/disk_ata/ata.cpp's BusContext).
struct I8042Context {
  uint16_t ioBase = 0;// data/status/command port base (0x60), resolved from the grant

  // IRQ1 (keyboard) is normally granted through the same registry
  // reservation as the port range (see config.py / legacy_isa_enum.cpp);
  // IRQ12 (aux/mouse) has no registry-driven grant since a reservation only
  // carries a single Irq value, so it stays at its hardcoded default.
  uint32_t irqKeyboard = i8042_regs::kDefaultIrqKeyboard;
  uint32_t irqMouse = i8042_regs::kDefaultIrqMouse;

  // \Device\Keyboard0 / \Device\PointerPort0. Each carries its own
  // KeyboardDeviceContext / PointerDeviceContext in its deviceExtension.
  kernel::KDevicePtr<kernel::KDevice> keyboardDevice{nullptr};
  kernel::KDevicePtr<kernel::KDevice> mouseDevice{nullptr};

  // Kept alive here so the connected ISRs stay attached for the lifetime of
  // the FDO -- dropping a KObjectPtr<KInterruptServiceRoutineObject> frees a
  // live ISR object (see KE_IO_ConnectInterrupt's contract in io.hpp).
  kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> keyboardIsr{nullptr};
  kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> mouseIsr{nullptr};

  // Deferral: both ISRs signal `wake` after buffering bytes, and the pump
  // thread parks on it. Auto-reset, so the signal is sticky and a byte that
  // arrives between the pump observing empty rings and actually parking
  // cannot be lost -- the pattern kernel/src/pnp_manager.cpp documents, and
  // the one the old KE_SCHED_Block/Unblock pair could not guarantee.
  kernel::KDispatcherHeader wake{};
  kernel::KObjectPtr<kernel::KThreadObject> pumpThread{nullptr};
};
