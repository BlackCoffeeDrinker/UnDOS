
#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>

namespace kernel {
struct KProcessObject;

enum class ProcessEvent : uint8_t { Creating, Exiting };
using ProcessNotifyFn = kstd::error_code (*)(ProcessEvent, KProcessObject &, void *ctx) noexcept;

// Fixed order slots; lower runs first on Exiting, last on Creating.
enum class ProcessNotifyOrder : uint8_t { User = 10, Gfx = 20 };

// Init-time only, never unregistered. Errors: ResourceLimitExceeded (array full), InvalidState (duplicate order).
kstd::error_code KE_PS_RegisterNotify(ProcessNotifyFn fn, void *ctx, ProcessNotifyOrder order) noexcept;
}// namespace kernel
