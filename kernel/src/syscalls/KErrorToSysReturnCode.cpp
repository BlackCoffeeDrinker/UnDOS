
#include "KErrorToSysReturnCode.hpp"

namespace kernel {
SyscallReturnCode KErrorToSysReturnCode(const kstd::error_code &error) {
  if (!error) return SyscallReturnCode_OKAY;
  if (error.category() != kernel_error_category()) return SyscallReturnCode_UNKNOWN;
  switch (static_cast<KernelError>(error.value())) {
    case KernelError::Success:
      return SyscallReturnCode_OKAY;

    case KernelError::InvalidSize:
    case KernelError::NameTooLong:
    case KernelError::BufferTooSmall:
      return SyscallReturnCode_INVALID_SIZE;

    case KernelError::OutOfMemory:
    case KernelError::AddressSpaceExhausted:
      return SyscallReturnCode_OUT_OF_MEMORY;

    case KernelError::InvalidAddress:
      return SyscallReturnCode_INVALID_ADDRESS;

    case KernelError::InvalidArgument:
    case KernelError::InvalidAlignment:
    case KernelError::InvalidOffset:
    case KernelError::OutOfRange:
    case KernelError::ArithmeticOverflow:
    case KernelError::InvalidFormat:
    case KernelError::TooManyArguments:
    case KernelError::InvalidHandle:
    case KernelError::WrongObjectType:
    case KernelError::AlreadyAttached:
    case KernelError::ParentMismatch:
    case KernelError::InvalidState:
    case KernelError::NotWaitable:
    case KernelError::InvalidData:
    case KernelError::InvalidResource:
    case KernelError::NotDirectory:
    case KernelError::InvalidExecutable:
      return SyscallReturnCode_INVALID_ARGUMENT;

    case KernelError::Unsupported:
    case KernelError::NotImplemented:
      return SyscallReturnCode_UNSUPPORTED;

    case KernelError::Timeout:
      return SyscallReturnCode_TIMEOUT;

    // The current syscall ABI has no equivalent for these failures.
    case KernelError::Unknown:
    case KernelError::ResourceLimitExceeded:
    case KernelError::QueueFull:
    case KernelError::ResourceConflict:
    case KernelError::NotFound:
    case KernelError::AlreadyExists:
    case KernelError::AccessDenied:
    case KernelError::NotInitialized:
    case KernelError::NoCurrentThread:
    case KernelError::NoCurrentProcess:
    case KernelError::DeviceNotReady:
    case KernelError::StandardStreamUnavailable:
    case KernelError::WouldBlock:
    case KernelError::IoError:
    case KernelError::DeviceError:
    case KernelError::UnexpectedEndOfFile:
    case KernelError::UnresolvedSymbol:
    case KernelError::ProcessStartupFailed:
      return SyscallReturnCode_UNKNOWN;
  }
  return SyscallReturnCode_UNKNOWN;
}

}// namespace kernel
