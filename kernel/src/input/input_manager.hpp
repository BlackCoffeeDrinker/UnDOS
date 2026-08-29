#pragma once

namespace kernel::input {
// Initializes the kernel input manager's raw byte rings. Called once from
// kernel_core_main, right after kernel::resource::init() and before drivers
// (e.g. input_i8042) are loaded, so KE_INPUT_ReportKeyboardData/ReportAuxData
// are always safe to call from an ISR by the time a driver connects one.
void init();
}// namespace kernel::input
