
#include "ps2_pump.hpp"

#include <span.hpp>

#include <kernel/scheduler.hpp>

#include "hid_parser.hpp"

namespace {

// Drains the keyboard ring. Each decoded transition updates the held-usage
// set and, when that set actually changed, renders a fresh HID keyboard
// report into the shared parser. Returns true if any byte was consumed.
bool DrainKeyboard(const kernel::KDevicePtr<kernel::KDevice> &device) {
  if (!device) return false;
  auto *ctx = device->deviceExtension.as<KeyboardDeviceContext>();
  if (ctx == nullptr) return false;

  bool didWork = false;
  uint8_t byte = 0;
  while (ctx->bytes.Pop(byte)) {
    didWork = true;

    ps2::KeyTransition transition;
    if (!ctx->translator.Feed(byte, transition)) continue;
    if (!ctx->builder.Apply(transition)) continue;

    uint8_t report[ps2::kKeyReportBytes]{};
    ctx->builder.Pack(report);
    kernel::hid::SubmitReport(device, ctx->format, ctx->decoder,
                              kstd::span<const uint8_t>(report, ps2::kKeyReportBytes));

    // Pause/Break has no break code on the wire, so the make must be
    // followed immediately by a release or the key would stay down forever.
    if (transition.usage == kernel::hid::usage_kbd::Pause && !transition.isBreak) {
      ctx->builder.Release(transition.usage);
      ctx->builder.Pack(report);
      kernel::hid::SubmitReport(device, ctx->format, ctx->decoder,
                                kstd::span<const uint8_t>(report, ps2::kKeyReportBytes));
    }
  }
  return didWork;
}

// Drains the aux ring, framing PS/2 packets and re-emitting each completed
// one as a synthetic HID mouse report.
bool DrainAux(const kernel::KDevicePtr<kernel::KDevice> &device) {
  if (!device) return false;
  auto *ctx = device->deviceExtension.as<PointerDeviceContext>();
  if (ctx == nullptr) return false;

  bool didWork = false;
  uint8_t byte = 0;
  while (ctx->bytes.Pop(byte)) {
    didWork = true;

    kernel::input::MousePacket packet;
    if (!ctx->decoder.Feed(byte, packet)) continue;

    uint8_t report[ps2::kMouseReportBytes]{};
    ps2::PackMouseReport(packet, report);
    kernel::hid::SubmitReport(device, ctx->format, ctx->hidState,
                              kstd::span<const uint8_t>(report, ps2::kMouseReportBytes));
  }
  return didWork;
}

}// namespace

namespace ps2 {

void I8042_PumpThread(void *arg) {
  auto *ctx = static_cast<I8042Context *>(arg);
  if (ctx == nullptr) return;

  for (;;) {
    // Both halves run unconditionally so a byte buffered for one device
    // never waits on the other having work.
    const bool keyboardWork = DrainKeyboard(ctx->keyboardDevice);
    const bool auxWork = DrainAux(ctx->mouseDevice);
    if (keyboardWork || auxWork) continue;

    // Auto-reset header: a byte pushed between the empty-ring checks above
    // and this park is not lost, because the ISR's signal is sticky and is
    // consumed here rather than being missed.
    (void) KE_SCHED_WaitForHeader(ctx->wake, kernel::WAIT_INFINITE);
  }
}

}// namespace ps2
