#pragma once

#include <kernel/kobject/KIOObject.hpp>

// Kernel-internal IO extension points. IO knows nothing about the providers
// (TTY, consoles, ...); they register here and IO only sees KIOObject streams.
namespace kernel::io {

// Adapts a foreign object into a stream. Returns WrongObjectType when the
// object is not handled by this adapter.
using ObjectAdapter = KIOObjectResult (*)(const KObjectPtr<KObject> &obj) noexcept;

// Opens a fresh stream on the system default console.
using DefaultConsoleProvider = KIOObjectResult (*)() noexcept;

// Safe from global constructors: the adapter table is constant-initialised.
kstd::error_code RegisterObjectAdapter(ObjectAdapter adapter) noexcept;

void SetDefaultConsoleProvider(DefaultConsoleProvider provider) noexcept;

// NotFound when no provider is registered or the provider has no console.
KIOObjectResult OpenDefaultConsole() noexcept;

}// namespace kernel::io
