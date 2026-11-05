#pragma once

#include <kernel/kerror.hpp>

namespace kernel::tty::detail {

kstd::error_code StartPumpThread() noexcept;

} // namespace kernel::tty::detail
