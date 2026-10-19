#pragma once

#include <stdint.h>
#include <system_error.hpp>

namespace kernel {

enum class KernelError : uint32_t {
  Success = 0,
  Unknown,

  // Arguments, sizes and representation.
  InvalidArgument,
  InvalidSize,
  InvalidAlignment,
  InvalidOffset,
  OutOfRange,
  ArithmeticOverflow,
  NameTooLong,
  BufferTooSmall,
  InvalidFormat,
  TooManyArguments,

  // Memory and bounded resources.
  InvalidAddress,
  OutOfMemory,
  AddressSpaceExhausted,
  ResourceLimitExceeded,
  QueueFull,
  ResourceConflict,

  // Handles, lookup, access and relationships.
  InvalidHandle,
  WrongObjectType,
  NotFound,
  AlreadyExists,
  AccessDenied,
  AlreadyAttached,
  ParentMismatch,

  // Execution context, lifecycle and availability.
  InvalidState,
  NotInitialized,
  NoCurrentThread,
  NoCurrentProcess,
  Unsupported,
  NotImplemented,
  DeviceNotReady,
  StandardStreamUnavailable,
  NotWaitable,

  // Waiting and nonblocking operations.
  Timeout,
  WouldBlock,

  // I/O, stored data and executable loading.
  IoError,
  DeviceError,
  UnexpectedEndOfFile,
  InvalidData,
  InvalidResource,
  NotDirectory,
  InvalidExecutable,
  UnresolvedSymbol,
  ProcessStartupFailed,
};

kstd::error_category &kernel_error_category();
kstd::error_code make_error_code(KernelError e);
}// namespace kernel
