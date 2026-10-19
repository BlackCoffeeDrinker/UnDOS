#pragma once

#include <kernel/kerror.hpp>

namespace kernel::input {
// Initializes the kernel input manager's event ring, gamepad snapshot store
// and \Device\Input stream. Called once from kernel_core_main, right after
// kernel::resource::init() and before drivers (e.g. input_i8042) are loaded,
// so KE_INPUT_PushEvent is always safe to call by the time a driver's worker
// thread starts reporting. The event ring is always usable afterwards; a
// failure to publish \Device\Input is returned.
KernelError init();
}// namespace kernel::input
