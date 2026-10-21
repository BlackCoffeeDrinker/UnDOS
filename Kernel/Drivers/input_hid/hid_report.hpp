#pragma once

#include <stdint.h>

#include "hid_usage_tables.hpp"

// Parsed shape of one HID input report. Produced once, at attach time, by
// ParseReportDescriptor; consumed per report by SubmitReport. Fixed-size
// and allocation-free so it can live inside a device context, and
// freestanding so it compiles into KernelTests.
namespace kernel::hid {

// One decoded field of a report: where its bits are and what control it is.
//
// A *variable* field maps to exactly one control (`usage` names it) and its
// value is the control's value. An *array* field is a slot that carries a
// usage ID as its value (the boot keyboard's key slots), in which case
// `usage` is 0 and logicalMin/logicalMax bound the usage range the slot may
// report.
struct ReportField {
  uint16_t usagePage = 0;
  uint16_t usage = 0;
  uint16_t bitOffset = 0;
  uint8_t bitSize = 0;
  bool isRelative = false;
  bool isArray = false;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
};

// Result of parsing a report descriptor once, at attach time.
struct ReportFormat {
  static constexpr size_t kMaxFields = 48;

  ReportField fields[kMaxFields]{};
  size_t fieldCount = 0;
  uint8_t reportId = 0;// 0 when the descriptor uses no report IDs
  size_t reportBytes = 0;
  uint16_t collectionUsage = 0;// usage_gd::Keyboard / Mouse / Joystick / Gamepad
};

// Per-device decoder state the HID layer needs across reports. Owned by the
// transport driver's device context, never a global: make/break for a
// keyboard comes from diffing consecutive reports, and the lock toggles are
// edge-triggered.
struct DecoderState {
  static constexpr size_t kMaxPressed = 16;

  uint16_t pressed[kMaxPressed]{};// usages held as of the previous report
  size_t pressedCount = 0;
  uint32_t lockModifiers = 0;// modifier_flags Caps/Num/Scroll, toggled on make
  uint8_t previousButtons = 0;
};

}// namespace kernel::hid
