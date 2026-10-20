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
  REQUIRE(handles.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read) == KernelError::Success);
  REQUIRE(handles.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write) == KernelError::Success);
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdin, result) == KernelError::Success);
  REQUIRE(result != unrelated);
  REQUIRE(handles.HasAccess(result, Access::Read));
  REQUIRE_FALSE(handles.HasAccess(result, Access::Write));
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdout, result) == KernelError::Success);
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
  REQUIRE(handles.SetStandardHandle(StandardStream::Stdout, handle) == KernelError::Success);
  REQUIRE(handles.SetStandardHandle(StandardStream::Stderr, handle) == KernelError::Success);
  REQUIRE(handles.Close(handle) == KernelError::Success);
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
  REQUIRE(parent.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read) == KernelError::Success);
  REQUIRE(parent.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write) == KernelError::Success);
  KHandle parentOut{};
  REQUIRE(parent.GetStandardHandle(StandardStream::Stdout, parentOut) == KernelError::Success);
  REQUIRE(parent.SetStandardHandle(StandardStream::Stderr, parentOut) == KernelError::Success);
  REQUIRE(child->InheritHandlesFrom(parent) == KernelError::Success);
  REQUIRE(child->handles.entries.size() == 2);
  KHandle childOut{}, childErr{}, childIn{};
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdin, childIn) == KernelError::Success);
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut) == KernelError::Success);
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stderr, childErr) == KernelError::Success);
  REQUIRE(childOut != parentOut);
  REQUIRE(childErr == childOut);
  REQUIRE(child->handles.Lookup(childOut)->object == stream);
  REQUIRE(child->handles.HasAccess(childIn, Access::Read));
  REQUIRE(child->handles.HasAccess(childOut, Access::Write));
  REQUIRE(parent.Close(parentOut) == KernelError::Success);
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut) == KernelError::Success);
}

TEST_CASE("Attaching default streams records handles after unrelated allocations", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = TakeExpected(kernel::CreateAnonymousKObject<kernel::KIOObject>());
  auto process = TakeExpected(kernel::CreateAnonymousKObject<kernel::KProcessObject>());
  REQUIRE(stream != nullptr);
  REQUIRE(process != nullptr);
  (void) TakeExpected(process->handles.Allocate(stream, Access::All, Flags::None));
  REQUIRE(process->AttachStandardStreams(stream) == KernelError::Success);
  KHandle input{}, output{}, error{};
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stdin, input) == KernelError::Success);
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stdout, output) == KernelError::Success);
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stderr, error) == KernelError::Success);
  REQUIRE(static_cast<uint32_t>(input) == 1);
  REQUIRE(static_cast<uint32_t>(output) == 2);
  REQUIRE(static_cast<uint32_t>(error) == 3);
  REQUIRE(process->handles.HasAccess(input, Access::Read));
  REQUIRE_FALSE(process->handles.HasAccess(input, Access::Write));
  REQUIRE(process->handles.HasAccess(output, Access::Write));
  REQUIRE(process->handles.HasAccess(error, Access::Write));
}
