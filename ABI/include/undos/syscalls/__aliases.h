#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

typedef uint32_t HANDLER;
typedef uint32_t COLORREF;
/**
 * Opaque 32-bit process-local object handle, not a pointer or a stream index.
 * Zero is a valid handle. Passing a handle to another process requires an
 * explicit inheritance or duplication operation; its numeric value may change.
 * Syscall status determines whether an output handle is valid.
 */
typedef uint32_t HANDLE;
