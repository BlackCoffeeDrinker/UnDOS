#include <catch2/catch_test_macros.hpp>

#include <kernel/kerror.hpp>
#include "syscalls/KErrorToSysReturnCode.hpp"

TEST_CASE("Kernel errors preserve success and failure semantics", "[kernel-error]") {
  using kernel::KernelError;
  REQUIRE_FALSE(kernel::make_error_code(KernelError::Success));
  REQUIRE(kernel::make_error_code(KernelError::OutOfMemory));
  REQUIRE(kernel::make_error_code(KernelError::QueueFull).message() == "Queue full");
  REQUIRE(kernel::make_error_code(static_cast<KernelError>(UINT32_MAX)).message() == "Unknown kernel error");
}

TEST_CASE("Kernel error mapping preserves syscall status categories", "[kernel-error]") {
  using kernel::KernelError;
  using kernel::KErrorToSysReturnCode;
  REQUIRE(KErrorToSysReturnCode(KernelError::Success) == SyscallReturnCode_OKAY);
  REQUIRE(KErrorToSysReturnCode(KernelError::InvalidSize) == SyscallReturnCode_INVALID_SIZE);
  REQUIRE(KErrorToSysReturnCode(KernelError::BufferTooSmall) == SyscallReturnCode_INVALID_SIZE);
  REQUIRE(KErrorToSysReturnCode(KernelError::OutOfMemory) == SyscallReturnCode_OUT_OF_MEMORY);
  REQUIRE(KErrorToSysReturnCode(KernelError::AddressSpaceExhausted) == SyscallReturnCode_OUT_OF_MEMORY);
  REQUIRE(KErrorToSysReturnCode(KernelError::InvalidAddress) == SyscallReturnCode_INVALID_ADDRESS);
  REQUIRE(KErrorToSysReturnCode(KernelError::InvalidHandle) == SyscallReturnCode_INVALID_ARGUMENT);
  REQUIRE(KErrorToSysReturnCode(KernelError::Unsupported) == SyscallReturnCode_UNSUPPORTED);
  REQUIRE(KErrorToSysReturnCode(KernelError::NotImplemented) == SyscallReturnCode_UNSUPPORTED);
  REQUIRE(KErrorToSysReturnCode(KernelError::Timeout) == SyscallReturnCode_TIMEOUT);
  REQUIRE(KErrorToSysReturnCode(KernelError::QueueFull) == SyscallReturnCode_UNKNOWN);
  REQUIRE(KErrorToSysReturnCode(KernelError::WouldBlock) == SyscallReturnCode_UNKNOWN);
  REQUIRE(KErrorToSysReturnCode(static_cast<KernelError>(UINT32_MAX)) == SyscallReturnCode_UNKNOWN);
}

TEST_CASE("Error code mapping checks the category before its value", "[kernel-error]") {
  using kernel::KernelError;
  using kernel::KErrorToSysReturnCode;
  REQUIRE(KErrorToSysReturnCode(kernel::make_error_code(KernelError::InvalidSize)) ==
          SyscallReturnCode_INVALID_SIZE);
  const kstd::error_code foreign(static_cast<int>(KernelError::InvalidSize), kstd::generic_category());
  REQUIRE(KErrorToSysReturnCode(foreign) == SyscallReturnCode_UNKNOWN);
  REQUIRE(KErrorToSysReturnCode(kstd::error_code{}) == SyscallReturnCode_OKAY);
}
