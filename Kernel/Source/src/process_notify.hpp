
#pragma once

#include <kernel/process_notify.hpp>

namespace kernel {
// Thread context; caller holds no OM, process-directory, scene, or DC lock.
// Creating: ascending order, stops at first error and sends Exiting to those that succeeded, returns the error.
// Exiting: ascending order (USER before GFX), every subscriber runs, errors are ignored.
kstd::error_code ps_notify(ProcessEvent event, KProcessObject &process) noexcept;
}// namespace kernel
