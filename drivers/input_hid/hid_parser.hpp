#pragma once

#include <span.hpp>
#include <stdint.h>

#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/input.hpp>

#include "hid_report.hpp"

// Shared HID class driver: a report-descriptor parser and a report-byte
// decoder. This is the only component in the system that calls
// KE_INPUT_PushEvent, so there is exactly one place that answers "what does
// this control mean" -- whether the bytes came off a USB interrupt endpoint
// or were packed by a legacy driver from a synthetic descriptor.
namespace kernel::hid {

// Parses a HID report descriptor (short items only) into `out`. Returns
// false on anything malformed -- a truncated item, a length running past
// the buffer, an unterminated collection, or more fields than
// ReportFormat::kMaxFields -- leaving `out` unusable rather than
// half-populated. Never allocates. Called once, at attach time, never per
// report and never from an ISR.
bool ParseReportDescriptor(kstd::span<const uint8_t> descriptor, ReportFormat &out);

// Decodes one report against a parsed format and pushes the resulting
// KInputReport(s) for `device`. `state` is the reporting device's decoder
// state (previous key set, lock toggles, previous button mask) and must be
// the same object across every report from that device.
//
// Allocation-free and bounded, but PASSIVE LEVEL ONLY: it calls
// KE_INPUT_PushEvent. A report shorter than format.reportBytes is rejected
// without reading past the buffer.
void SubmitReport(const kernel::KDevicePtr<kernel::KDevice> &device,
                  const ReportFormat &format, DecoderState &state,
                  kstd::span<const uint8_t> report);

// Extracts one field's value from a report, sign-extending when the field's
// logical range is signed. Exposed for the hosted tests; SubmitReport uses
// it internally.
[[nodiscard]] int32_t ExtractField(const ReportField &field, kstd::span<const uint8_t> report) noexcept;

// Builds the KeyboardInputEvent for one usage transition, applying the
// usage -> VK_*/ASCII tables and the supplied modifier bitmask. Exposed so
// the hosted tests can pin the mapping without going through the kernel.
[[nodiscard]] kernel::input::KeyboardInputEvent MakeKeyEvent(uint16_t usage, bool isBreak,
                                                             uint32_t modifiers) noexcept;

}// namespace kernel::hid
