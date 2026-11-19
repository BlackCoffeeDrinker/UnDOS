#pragma once

#include <kernel/kobject/KIOObject.hpp>

// Kernel-internal PS extension point. PS knows nothing about consoles; core
// registers the provider that opens default std streams for new processes.
namespace kernel::ps {

using StdioProvider = KIOObjectResult (*)() noexcept;

void SetStdioProvider(StdioProvider provider) noexcept;

// NotFound when no provider is registered.
KIOObjectResult OpenDefaultStdio() noexcept;

}// namespace kernel::ps
