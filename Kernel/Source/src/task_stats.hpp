#pragma once

#include "syscalls.hpp"

namespace kernel::task_stats {
kstd::error_code capture(__KRN_PROC_GETTASKSNAPSHOT_V1 &args) noexcept;
}
