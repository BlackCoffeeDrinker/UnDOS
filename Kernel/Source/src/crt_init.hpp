#pragma once

namespace kernel::crt {

// Runs the kernel image's global constructor table exactly once. Must be the
// first call in kernel_core_main (constructors may not use the HAL).
void run_global_constructors() noexcept;

// True when the walk ran this image's marker constructor.
bool global_constructors_ok() noexcept;

}// namespace kernel::crt
