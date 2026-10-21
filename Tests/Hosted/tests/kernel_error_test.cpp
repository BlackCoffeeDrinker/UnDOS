#include <catch2/catch_test_macros.hpp>

#include <kernel/kerror.hpp>
#include <expected.hpp>
#include <type_traits.hpp>
#include "syscalls/KErrorToSysReturnCode.hpp"

namespace error_test {
enum class Error { Failed = 1 };

kstd::error_code make_error_code(Error error) noexcept {
  return {static_cast<int>(error), kstd::generic_category()};
}
}

namespace kstd {
template <>
struct is_error_code_enum<error_test::Error> : true_type {};
}

namespace {
enum class UnregisteredError { Failed = 1 };
static_assert(kstd::is_error_code_enum_v<kernel::KernelError>);
static_assert(kstd::is_convertible_v<kernel::KernelError, kstd::error_code>);
static_assert(!kstd::is_convertible_v<UnregisteredError, kstd::error_code>);
static_assert(!kstd::is_convertible_v<int, kstd::error_code>);

kstd::error_code StatusError() {
  return kernel::KernelError::InvalidArgument;
}

template <class T>
kstd::expected<T, kstd::error_code> ExpectedError() {
  return kstd::unexpected(kernel::KernelError::InvalidArgument);
}
}

TEST_CASE("Error code enum conversion supports non-kernel domains", "[kernel-error][expected]") {
  kstd::error_code status = error_test::Error::Failed;
  REQUIRE(status == error_test::Error::Failed);
  REQUIRE(error_test::Error::Failed == status);
  REQUIRE(&status.category() == &kstd::generic_category());
  REQUIRE(status != kernel::KernelError::Unknown);
  status.clear();
  REQUIRE_FALSE(status);
  status = error_test::Error::Failed;
  const kstd::expected<int, kstd::error_code> result = kstd::unexpected(error_test::Error::Failed);
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == status);
}

TEST_CASE("Kernel error enums convert to generic status and expected errors", "[kernel-error][expected]") {
  using kernel::KernelError;
  auto status = StatusError();
  REQUIRE(status == KernelError::InvalidArgument);
  REQUIRE(KernelError::InvalidArgument == status);
  REQUIRE(&status.category() == &kernel::kernel_error_category());
  status = KernelError::Timeout;
  REQUIRE(status == KernelError::Timeout);
  status = KernelError::Success;
  REQUIRE_FALSE(status);

  const auto unexpected = kstd::unexpected(KernelError::InvalidArgument);
  const kstd::expected<int, kstd::error_code> value = unexpected;
  REQUIRE_FALSE(value);
  REQUIRE(value.error() == KernelError::InvalidArgument);
  const kstd::expected<void, kstd::error_code> empty = unexpected;
  REQUIRE_FALSE(empty);
  REQUIRE(empty.error() == KernelError::InvalidArgument);
  REQUIRE(ExpectedError<int>().error() == value.error());
  REQUIRE(ExpectedError<void>().error() == empty.error());
}

TEST_CASE("Expected errors retain non-kernel categories through propagation", "[kernel-error][expected]") {
  using Result = kstd::expected<int, kstd::error_code>;
  const kstd::error_code foreign(static_cast<int>(kernel::KernelError::InvalidArgument), kstd::generic_category());
  auto propagate = [](const Result &result) -> kstd::expected<void, kstd::error_code> {
    if (!result) return kstd::unexpected(result.error());
    return {};
  };
  const Result result = kstd::unexpected(foreign);
  const auto forwarded = propagate(result);
  REQUIRE_FALSE(forwarded);
  REQUIRE(forwarded.error() == foreign);
  REQUIRE(forwarded.error() != kernel::KernelError::InvalidArgument);
  REQUIRE(&forwarded.error().category() == &foreign.category());
}

TEST_CASE("Kernel errors preserve success and failure semantics", "[kernel-error]") {
  using kernel::KernelError;
  REQUIRE_FALSE(kernel::make_error_code(KernelError::Success));
  REQUIRE(kernel::make_error_code(KernelError::OutOfMemory));
  REQUIRE(kernel::make_error_code(KernelError::QueueFull).message() == "Queue full");
  REQUIRE(kernel::make_error_code(static_cast<KernelError>(UINT32_MAX)).message() == "Unknown kernel error");
}

TEST_CASE("Zero status codes succeed without erasing category identity", "[kernel-error]") {
  const kstd::error_code system;
  const kstd::error_code generic(0, kstd::generic_category());
  const auto kernel = kernel::make_error_code(kernel::KernelError::Success);

  REQUIRE_FALSE(system);
  REQUIRE_FALSE(generic);
  REQUIRE_FALSE(kernel);
  REQUIRE(system != generic);
  REQUIRE(system != kernel);
  REQUIRE(generic != kernel);
}

TEST_CASE("Generic error categories retain identity across default construction and clearing", "[kernel-error]") {
  const auto &generic = kstd::generic_category();
  const auto &system = kstd::system_category();
  REQUIRE(&kstd::generic_category() == &generic);
  REQUIRE(&kstd::system_category() == &system);
  REQUIRE(&generic != &system);
  REQUIRE(kstd::string_view(generic.name()) == "generic");
  REQUIRE(kstd::string_view(system.name()) == "system");

  kstd::error_code code;
  REQUIRE_FALSE(code);
  REQUIRE(&code.category() == &system);
  code = {1, generic};
  REQUIRE(code);
  code.clear();
  REQUIRE_FALSE(code);
  REQUIRE(&code.category() == &system);

  kstd::error_condition condition;
  REQUIRE_FALSE(condition);
  REQUIRE(&condition.category() == &generic);
  condition = {1, system};
  condition.clear();
  REQUIRE_FALSE(condition);
  REQUIRE(&condition.category() == &generic);
}

TEST_CASE("Kernel error category retains a stable lifetime and identity", "[kernel-error]") {
  auto &category = kernel::kernel_error_category();
  REQUIRE(&kernel::kernel_error_category() == &category);
  REQUIRE(kstd::string_view(category.name()) == "kernel_error_category");
  const auto first = kernel::make_error_code(kernel::KernelError::InvalidArgument);
  const auto second = kernel::make_error_code(kernel::KernelError::Timeout);
  REQUIRE(&first.category() == &category);
  REQUIRE(&second.category() == &category);
  REQUIRE(first.message() == "Invalid argument");
  REQUIRE(second.message() == "Operation timed out");
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
  REQUIRE(KErrorToSysReturnCode(KernelError::StandardStreamUnavailable) == SyscallReturnCode_INVALID_ARGUMENT);
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
