#pragma once

#include "i8042_context.hpp"

// The decoding half of the i8042 driver. Everything here runs at passive
// level on a driver-owned kernel thread: the ISRs do nothing but capture
// bytes into the per-device rings and signal `I8042Context::wake`.
namespace ps2 {

// Thread entry. Drains both device rings, feeds the owning decoder, packs a
// synthetic HID report and hands it to the shared HID class driver; parks on
// the auto-reset wake header when both rings are empty. Never returns.
void I8042_PumpThread(void *arg);

}// namespace ps2
