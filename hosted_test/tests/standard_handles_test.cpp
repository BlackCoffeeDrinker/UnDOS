#include <catch2/catch_test_macros.hpp>

#include "stdkrn.hpp"
#include "ob_test_support.hpp"
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>

namespace {
using kernel::KHandle;
using kernel::ProcessHandleTable;
using kernel::StandardStream;
using Access = ProcessHandleTable::HandleAccess;
using Flags = ProcessHandleTable::HandleFlags;
}

TEST_CASE("Standard handles are explicit opaque mappings", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = kernel::CreateAnonymousKObject<kernel::KIOObject>();
  REQUIRE(stream != nullptr);
  ProcessHandleTable handles;
  KHandle result{};
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stdin, result));
  REQUIRE_FALSE(handles.SetStandardHandle(StandardStream::Stdout, static_cast<KHandle>(12345)));
  REQUIRE_FALSE(handles.AllocateStandardHandle(StandardStream::Stdout, nullptr, Access::Write));
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stdout, result));

  const auto unrelated = handles.Allocate(stream, Access::All, Flags::None);
  REQUIRE(handles.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read));
  REQUIRE(handles.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write));
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdin, result));
  REQUIRE(result != unrelated);
  REQUIRE(handles.HasAccess(result, Access::Read));
  REQUIRE_FALSE(handles.HasAccess(result, Access::Write));
  REQUIRE(handles.GetStandardHandle(StandardStream::Stdout, result));
  REQUIRE(handles.HasAccess(result, Access::Write));
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stderr, result));
  REQUIRE_FALSE(handles.GetStandardHandle(static_cast<StandardStream>(3), result));
}

TEST_CASE("Closing standard handles clears all aliases before reuse", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = kernel::CreateAnonymousKObject<kernel::KIOObject>();
  REQUIRE(stream != nullptr);
  ProcessHandleTable handles;
  const auto handle = handles.Allocate(stream, Access::Write, Flags::Inheritable);
  REQUIRE(handles.SetStandardHandle(StandardStream::Stdout, handle));
  REQUIRE(handles.SetStandardHandle(StandardStream::Stderr, handle));
  REQUIRE(handles.Close(handle));
  REQUIRE(handles.Allocate(stream, Access::Read, Flags::None) == handle);
  KHandle result{};
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stdout, result));
  REQUIRE_FALSE(handles.GetStandardHandle(StandardStream::Stderr, result));
}

TEST_CASE("Standard handle inheritance remaps process-local values", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = kernel::CreateAnonymousKObject<kernel::KIOObject>();
  auto child = kernel::CreateAnonymousKObject<kernel::KProcessObject>();
  REQUIRE(stream != nullptr);
  REQUIRE(child != nullptr);
  ProcessHandleTable parent;
  parent.Allocate(stream, Access::All, Flags::None);
  REQUIRE(parent.AllocateStandardHandle(StandardStream::Stdin, stream, Access::Read));
  REQUIRE(parent.AllocateStandardHandle(StandardStream::Stdout, stream, Access::Write));
  KHandle parentOut{};
  REQUIRE(parent.GetStandardHandle(StandardStream::Stdout, parentOut));
  REQUIRE(parent.SetStandardHandle(StandardStream::Stderr, parentOut));
  REQUIRE(child->InheritHandlesFrom(parent));
  REQUIRE(child->handles.entries.size() == 2);
  KHandle childOut{}, childErr{}, childIn{};
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdin, childIn));
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut));
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stderr, childErr));
  REQUIRE(childOut != parentOut);
  REQUIRE(childErr == childOut);
  REQUIRE(child->handles.Lookup(childOut)->object == stream);
  REQUIRE(child->handles.HasAccess(childIn, Access::Read));
  REQUIRE(child->handles.HasAccess(childOut, Access::Write));
  REQUIRE(parent.Close(parentOut));
  REQUIRE(child->handles.GetStandardHandle(StandardStream::Stdout, childOut));
}

TEST_CASE("Attaching default streams records handles after unrelated allocations", "[standard-handles]") {
  EnsureObjectManagerInitialized();
  auto stream = kernel::CreateAnonymousKObject<kernel::KIOObject>();
  auto process = kernel::CreateAnonymousKObject<kernel::KProcessObject>();
  REQUIRE(stream != nullptr);
  REQUIRE(process != nullptr);
  process->handles.Allocate(stream, Access::All, Flags::None);
  REQUIRE(process->AttachStandardStreams(stream));
  KHandle input{}, output{}, error{};
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stdin, input));
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stdout, output));
  REQUIRE(process->handles.GetStandardHandle(StandardStream::Stderr, error));
  REQUIRE(static_cast<uint32_t>(input) == 1);
  REQUIRE(static_cast<uint32_t>(output) == 2);
  REQUIRE(static_cast<uint32_t>(error) == 3);
  REQUIRE(process->handles.HasAccess(input, Access::Read));
  REQUIRE_FALSE(process->handles.HasAccess(input, Access::Write));
  REQUIRE(process->handles.HasAccess(output, Access::Write));
  REQUIRE(process->handles.HasAccess(error, Access::Write));
}
