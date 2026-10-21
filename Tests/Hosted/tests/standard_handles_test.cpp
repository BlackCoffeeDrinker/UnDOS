#include <catch2/catch_test_macros.hpp>

#include "stdkrn.hpp"
#include "ob_test_support.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>

namespace {
using kernel::KHandle;
using kernel::KernelError;
using kernel::ProcessHandleTable;
using kernel::StandardStream;
using Access = ProcessHandleTable::HandleAccess;
using Flags = ProcessHandleTable::HandleFlags;

template<typename T>
T TakeExpected(kstd::expected<T, kstd::error_code> result) {
  REQUIRE(result.has_value());
  return kstd::move(*result);
}
}

TEST_CASE("Standard handles are explicit opaque mappings", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = TakeExpected(kernel::CreateAnonymousKObject<kernel::KIOObject>());
  REQUIRE(stream != nullptr);
  ProcessHandleTable handles;
  KHandle result{};
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdin, result) == KernelError::StandardStreamUnavailable);
  REQUIRE(handles.SetStandardHandle(StandardStream::Stdout, static_cast<KHandle>(12345)) == KernelError::InvalidHandle);
  REQUIRE(handles.AllocateStandardHandle(StandardStream::Stdout, nullptr, Access::Write) == KernelError::InvalidArgument);
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdout, result) == KernelError::StandardStreamUnavailable);

  const auto unrelated = TakeExpected(handles.Allocate(stream, Access::All, Flags::None));
  REQUIRE_FALSE(handles.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read));
  REQUIRE_FALSE(handles.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write));
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stdin, result));
  REQUIRE(result != unrelated);
  REQUIRE(handles.HasAccess(result, Access::Read));
  REQUIRE_FALSE(handles.HasAccess(result, Access::Write));
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stdout, result));
  REQUIRE(handles.HasAccess(result, Access::Write));
  REQUIRE(handles.GetStandardHandle(StandardStream::Stderr, result) == KernelError::StandardStreamUnavailable);
  REQUIRE(handles.GetStandardHandle(static_cast<StandardStream>(3), result) == KernelError::InvalidArgument);
}

TEST_CASE("Closing standard handles clears all aliases before reuse", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = TakeExpected(kernel::CreateAnonymousKObject<kernel::KIOObject>());
  REQUIRE(stream != nullptr);
  ProcessHandleTable handles;
  const auto handle = TakeExpected(handles.Allocate(stream, Access::Write, Flags::Inheritable));
  REQUIRE_FALSE(handles.SetStandardHandle(StandardStream::Stdout, handle));
  REQUIRE_FALSE(handles.SetStandardHandle(StandardStream::Stderr, handle));
  REQUIRE_FALSE(handles.Close(handle));
  REQUIRE(TakeExpected(handles.Allocate(stream, Access::Read, Flags::None)) == handle);
  KHandle result{};
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdout, result) == KernelError::StandardStreamUnavailable);
  REQUIRE(handles.GetStandardHandle(StandardStream::Stderr, result) == KernelError::StandardStreamUnavailable);
}

TEST_CASE("Standard handle inheritance remaps process-local values", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = TakeExpected(kernel::CreateAnonymousKObject<kernel::KIOObject>());
  auto child = TakeExpected(kernel::CreateAnonymousKObject<kernel::KProcessObject>());
  REQUIRE(stream != nullptr);
  REQUIRE(child != nullptr);
  ProcessHandleTable parent;
  (void) TakeExpected(parent.Allocate(stream, Access::All, Flags::None));
  REQUIRE_FALSE(parent.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read));
  REQUIRE_FALSE(parent.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write));
  KHandle parentOut{};
  REQUIRE_FALSE(parent.GetStandardHandle(StandardStream::Stdout, parentOut));
  REQUIRE_FALSE(parent.SetStandardHandle(StandardStream::Stderr, parentOut));
  REQUIRE_FALSE(child->InheritHandlesFrom(parent));
  REQUIRE(child->handles.entries.size() == 2);
  KHandle childOut{}, childErr{}, childIn{};
  REQUIRE_FALSE(child->handles.GetStandardHandle(StandardStream::Stdin, childIn));
  REQUIRE_FALSE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut));
  REQUIRE_FALSE(child->handles.GetStandardHandle(StandardStream::Stderr, childErr));
  REQUIRE(childOut != parentOut);
  REQUIRE(childErr == childOut);
  REQUIRE(child->handles.Lookup(childOut)->object == stream);
  REQUIRE(child->handles.HasAccess(childIn, Access::Read));
  REQUIRE(child->handles.HasAccess(childOut, Access::Write));
  REQUIRE_FALSE(parent.Close(parentOut));
  REQUIRE_FALSE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut));
}

TEST_CASE("Attaching default streams records handles after unrelated allocations", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = TakeExpected(kernel::CreateAnonymousKObject<kernel::KIOObject>());
  auto process = TakeExpected(kernel::CreateAnonymousKObject<kernel::KProcessObject>());
  REQUIRE(stream != nullptr);
  REQUIRE(process != nullptr);
  (void) TakeExpected(process->handles.Allocate(stream, Access::All, Flags::None));
  REQUIRE_FALSE(process->AttachStandardStreams(stream));
  KHandle input{}, output{}, error{};
  REQUIRE_FALSE(process->handles.GetStandardHandle(StandardStream::Stdin, input));
  REQUIRE_FALSE(process->handles.GetStandardHandle(StandardStream::Stdout, output));
  REQUIRE_FALSE(process->handles.GetStandardHandle(StandardStream::Stderr, error));
  REQUIRE(static_cast<uint32_t>(input) == 1);
  REQUIRE(static_cast<uint32_t>(output) == 2);
  REQUIRE(static_cast<uint32_t>(error) == 3);
  REQUIRE(process->handles.HasAccess(input, Access::Read));
  REQUIRE_FALSE(process->handles.HasAccess(input, Access::Write));
  REQUIRE(process->handles.HasAccess(output, Access::Write));
  REQUIRE(process->handles.HasAccess(error, Access::Write));
}
