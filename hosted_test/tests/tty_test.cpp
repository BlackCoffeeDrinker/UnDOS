#include <catch2/catch_test_macros.hpp>
#include <kernel/tty/tty.hpp>
#include <kernel/tty/tty_device.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/device.hpp>
#include <kernel/driver.hpp>
#include <kernel/io.hpp>
#include <kernel/object_manager.hpp>
#include "ob_test_support.hpp"
#include <string.h>

TEST_CASE("Blocking TTY reads distinguish data and EOF", "[tty][blocking]") {
  kernel::tty::KVtty tty;
  uint8_t buffer[16]{};
  size_t transferred = 99;
  REQUIRE(tty.sink_read_wait(buffer, 0, transferred) == kernel::KernelError::Success);
  REQUIRE(transferred == 0);

  SECTION("Committed data is available without waiting") {
    const uint8_t input[] = {'o', 'k', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));
    REQUIRE(tty.sink_read_wait(buffer, 2, transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 2);
    REQUIRE(memcmp(buffer, "ok", 2) == 0);
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 1);
    REQUIRE(buffer[0] == '\n');
  }
  SECTION("Ctrl+D EOF is consumed without closing the next session") {
    const uint8_t eof = 4;
    REQUIRE(tty.source_write(&eof, 1).value() == 1);
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 0);
    const uint8_t input[] = {'x', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 2);
    REQUIRE(memcmp(buffer, input, transferred) == 0);
  }
  SECTION("Closing source produces EOF") {
    tty.close_source();
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 0);
  }
  SECTION("Closing sink produces EOF") {
    tty.close_sink();
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) == kernel::KernelError::Success);
    REQUIRE(transferred == 0);
  }
  SECTION("A failed wait is not reported as EOF") {
    // The hosted scheduler cannot block and returns Timeout on an empty header.
    REQUIRE(tty.sink_read_wait(buffer, sizeof(buffer), transferred) != kernel::KernelError::Success);
    REQUIRE(transferred == 0);
  }
}

TEST_CASE("RingBuffer basic push, pop, read, write semantics", "[tty][ringbuffer]") {
  kernel::tty::RingBuffer<uint8_t, 8> ring;
  REQUIRE(ring.empty());
  REQUIRE_FALSE(ring.full());
  REQUIRE(ring.size() == 0);
  REQUIRE(ring.capacity() == 8);
  REQUIRE(ring.available() == 8);

  // Push elements
  for (uint8_t i = 1; i <= 8; ++i) {
    REQUIRE(ring.push(i));
  }
  REQUIRE(ring.full());
  REQUIRE(ring.size() == 8);
  REQUIRE(ring.available() == 0);
  REQUIRE_FALSE(ring.push(9)); // Should fail when full

  // Pop elements
  uint8_t val = 0;
  for (uint8_t i = 1; i <= 4; ++i) {
    REQUIRE(ring.pop(val));
    REQUIRE(val == i);
  }
  REQUIRE(ring.size() == 4);
  REQUIRE(ring.available() == 4);

  // Bulk write that wraps around
  const uint8_t writeBuf[4] = {10, 11, 12, 13};
  REQUIRE(ring.write(writeBuf, 4) == 4);
  REQUIRE(ring.full());

  // Bulk read
  uint8_t readBuf[8] = {0};
  REQUIRE(ring.read(readBuf, 8) == 8);
  REQUIRE(ring.empty());

  const uint8_t expected[8] = {5, 6, 7, 8, 10, 11, 12, 13};
  REQUIRE(memcmp(readBuf, expected, 8) == 0);

  // Clear
  ring.push(42);
  REQUIRE(ring.size() == 1);
  ring.clear();
  REQUIRE(ring.empty());
}

TEST_CASE("KVtty canonical mode line editing and echo", "[tty][canonical]") {
  kernel::tty::KVtty tty;
  // Default config: TTY_ECHO | TTY_CANONICAL | TTY_CR_TO_NL | TTY_NL_TO_CRNL

  SECTION("Scenario 1: Backspace editing and visual erase sequences") {
    // Source writes abc\b\bde\n
    const uint8_t input[] = {'a', 'b', 'c', '\b', '\b', 'd', 'e', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    // Sink reads committed line: expects "ade\n"
    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 4);
    REQUIRE(memcmp(sinkBuf, "ade\n", 4) == 0);

    // Source reads echo: 'a', 'b', 'c', '\b \b', '\b \b', 'd', 'e', '\r\n'
    uint8_t sourceBuf[64] = {0};
    size_t nEcho = tty.source_read(sourceBuf, sizeof(sourceBuf));
    const uint8_t expectedEcho[] = {
      'a', 'b', 'c',
      '\b', ' ', '\b',
      '\b', ' ', '\b',
      'd', 'e',
      '\r', '\n'
    };
    REQUIRE(nEcho == sizeof(expectedEcho));
    REQUIRE(memcmp(sourceBuf, expectedEcho, sizeof(expectedEcho)) == 0);
  }

  SECTION("DEL (0x7F) backspace behaves identically") {
    const uint8_t input[] = {'x', 'y', 0x7F, 'z', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 3);
    REQUIRE(memcmp(sinkBuf, "xz\n", 3) == 0);
  }

  SECTION("Backspace on empty line is a harmless no-op") {
    const uint8_t input[] = {'\b', '\b', 'o', 'k', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 3);
    REQUIRE(memcmp(sinkBuf, "ok\n", 3) == 0);

    // Echo should only contain 'o', 'k', '\r\n' (no stray erase sequences for the initial backspaces)
    uint8_t sourceBuf[32] = {0};
    size_t nEcho = tty.source_read(sourceBuf, sizeof(sourceBuf));
    const uint8_t expectedEcho[] = {'o', 'k', '\r', '\n'};
    REQUIRE(nEcho == sizeof(expectedEcho));
    REQUIRE(memcmp(sourceBuf, expectedEcho, sizeof(expectedEcho)) == 0);
  }

  SECTION("Ctrl+U clears the entire line edit buffer") {
    const uint8_t input[] = {'w', 'r', 'o', 'n', 'g', 0x15, 'r', 'i', 'g', 'h', 't', '\n'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 6);
    REQUIRE(memcmp(sinkBuf, "right\n", 6) == 0);
  }

  SECTION("Ctrl+D on non-empty buffer commits line without newline") {
    const uint8_t input[] = {'h', 'e', 'l', 'l', 'o', 0x04};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 5);
    REQUIRE(memcmp(sinkBuf, "hello", 5) == 0);
  }

  SECTION("Ctrl+D on empty buffer signals EOF") {
    const uint8_t input[] = {0x04};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 0);
  }

  SECTION("CR to NL input translation (\\r -> \\n)") {
    const uint8_t input[] = {'t', 'e', 's', 't', '\r'};
    REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

    uint8_t sinkBuf[32] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == 5);
    REQUIRE(memcmp(sinkBuf, "test\n", 5) == 0);
  }

  SECTION("Line longer than canonical edit buffer capacity does not corrupt memory") {
    // Write 1100 'A's followed by '\n'
    uint8_t longInput[1101];
    memset(longInput, 'A', 1100);
    longInput[1100] = '\n';
    REQUIRE(tty.source_write(longInput, sizeof(longInput)).value() == sizeof(longInput));

    // Edit buffer max is 1024, so it should safely hold at most 1024 bytes (1023 'A's + '\n')
    uint8_t sinkBuf[1101] = {0};
    size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
    REQUIRE(nRead == kernel::tty::kMaxEditBufferSize);
    REQUIRE(sinkBuf[nRead - 1] == '\n');
    REQUIRE(sinkBuf[0] == 'A');
  }
}

TEST_CASE("KVtty raw mode pass-through", "[tty][raw]") {
  kernel::tty::KVtty tty;
  tty.set_flags(0); // Raw mode: no canonical, no echo, no translations

  const uint8_t input[] = {'a', 'b', 'c', 0x03, 0x04, '\b'};
  REQUIRE(tty.source_write(input, sizeof(input)).value() == sizeof(input));

  // In raw mode, data is immediately available to sink without waiting for \n
  REQUIRE(tty.sink_has_data());
  uint8_t sinkBuf[32] = {0};
  size_t nRead = tty.sink_read(sinkBuf, sizeof(sinkBuf));
  REQUIRE(nRead == sizeof(input));
  REQUIRE(memcmp(sinkBuf, input, sizeof(input)) == 0);

  // With echo disabled, source read buffer should be empty
  REQUIRE_FALSE(tty.source_has_data());
}

TEST_CASE("KVtty sink write output translations", "[tty][sink_write]") {
  kernel::tty::KVtty tty;

  SECTION("Newline to CRLF translation enabled (default)") {
    const uint8_t msg[] = "Line 1\nLine 2\n";
    constexpr size_t msgLen = sizeof(msg) - 1;
    REQUIRE(tty.sink_write(msg, msgLen).value() == msgLen);

    uint8_t outBuf[64] = {0};
    size_t nRead = tty.source_read(outBuf, sizeof(outBuf));
    const char *expected = "Line 1\r\nLine 2\r\n";
    REQUIRE(nRead == strlen(expected));
    REQUIRE(memcmp(outBuf, expected, nRead) == 0);
  }

  SECTION("Newline to CRLF translation disabled") {
    tty.set_flags(kernel::tty::TTY_CANONICAL);
    const uint8_t msg[] = "Raw\nOutput\n";
    constexpr size_t msgLen = sizeof(msg) - 1;
    REQUIRE(tty.sink_write(msg, msgLen).value() == msgLen);

    uint8_t outBuf[64] = {0};
    size_t nRead = tty.source_read(outBuf, sizeof(outBuf));
    REQUIRE(nRead == msgLen);
    REQUIRE(memcmp(outBuf, msg, nRead) == 0);
  }
}

TEST_CASE("KVtty window size configuration and querying", "[tty][winsize]") {
  kernel::tty::KVtty tty;
  auto ws = tty.get_winsize();
  REQUIRE(ws.ws_row == 25);
  REQUIRE(ws.ws_col == 80);

  kernel::tty::WinSize newWs{.ws_row = 40, .ws_col = 120, .ws_xpixel = 1024, .ws_ypixel = 768};
  tty.set_winsize(newWs);

  auto queried = tty.get_winsize();
  REQUIRE(queried.ws_row == 40);
  REQUIRE(queried.ws_col == 120);
  REQUIRE(queried.ws_xpixel == 1024);
  REQUIRE(queried.ws_ypixel == 768);
}

TEST_CASE("KVtty closing endpoints", "[tty][close]") {
  SECTION("Closing source causes sink read to return EOF (0 bytes)") {
    kernel::tty::KVtty tty;
    REQUIRE_FALSE(tty.is_source_closed());
    tty.close_source();
    REQUIRE(tty.is_source_closed());

    uint8_t buf[16] = {0};
    REQUIRE(tty.sink_read(buf, sizeof(buf)) == 0);
  }

  SECTION("Closing sink stops writes") {
    kernel::tty::KVtty tty;
    REQUIRE_FALSE(tty.is_sink_closed());
    tty.close_sink();
    REQUIRE(tty.is_sink_closed());

    uint8_t buf[4] = {'a', 'b', 'c', '\n'};
    REQUIRE(tty.source_write(buf, sizeof(buf)) == kstd::unexpected(kernel::KernelError::InvalidState));
    REQUIRE(tty.sink_write(buf, sizeof(buf)) == kstd::unexpected(kernel::KernelError::InvalidState));
  }
}

TEST_CASE("Multiple KVtty instances are isolated", "[tty][isolation]") {
  kernel::tty::KVtty tty1;
  kernel::tty::KVtty tty2;

  const uint8_t msg1[] = "Hello TTY1\n";
  const uint8_t msg2[] = "Hello TTY2\n";

  REQUIRE(tty1.source_write(msg1, sizeof(msg1) - 1).value() == sizeof(msg1) - 1);
  REQUIRE(tty2.source_write(msg2, sizeof(msg2) - 1).value() == sizeof(msg2) - 1);

  uint8_t buf1[32] = {0};
  uint8_t buf2[32] = {0};

  size_t r1 = tty1.sink_read(buf1, sizeof(buf1));
  size_t r2 = tty2.sink_read(buf2, sizeof(buf2));

  REQUIRE(r1 == sizeof(msg1) - 1);
  REQUIRE(r2 == sizeof(msg2) - 1);
  REQUIRE(memcmp(buf1, msg1, r1) == 0);
  REQUIRE(memcmp(buf2, msg2, r2) == 0);
}

TEST_CASE("KVttySourceObject and KVttySinkObject lifecycle and pairing", "[tty][kobject]") {
  EnsureObjectManagerInitialized();

  kernel::tty::VttyPtr vtty(new kernel::tty::KVtty());
  REQUIRE(vtty != nullptr);
  REQUIRE(vtty->ref_count() == 1);

  auto source = kernel::CreateKObject<kernel::KVttySourceObject>("TestSource").value_or(nullptr);
  auto sink = kernel::CreateKObject<kernel::KVttySinkObject>("TestSink").value_or(nullptr);
  REQUIRE(source != nullptr);
  REQUIRE(sink != nullptr);

  source->vtty = vtty;
  sink->vtty = vtty;
  REQUIRE(vtty->ref_count() == 3);

  // Write via source, read via sink
  const uint8_t input[] = "Hello Object TTY\n";
  constexpr size_t inputLen = sizeof(input) - 1;
  REQUIRE(source->vtty->source_write(input, inputLen).value() == inputLen);

  uint8_t readBuf[32] = {0};
  size_t nRead = sink->vtty->sink_read(readBuf, sizeof(readBuf));
  REQUIRE(nRead == inputLen);
  REQUIRE(memcmp(readBuf, input, inputLen) == 0);

  // Close source through source object destruction/close
  source->vtty->close_source();
  REQUIRE(sink->vtty->is_source_closed());
  // Reading from sink when source is closed returns 0 (EOF)
  REQUIRE(sink->vtty->sink_read(readBuf, sizeof(readBuf)) == 0);
}

TEST_CASE("KProcessObject standard handle allocation and inheritance", "[tty][handles]") {
  EnsureObjectManagerInitialized();

  auto parent = kernel::CreateKObject<kernel::KProcessObject>("TestParentProc").value_or(nullptr);
  auto child = kernel::CreateKObject<kernel::KProcessObject>("TestChildProc").value_or(nullptr);
  auto sink = kernel::CreateKObject<kernel::KVttySinkObject>("TestSinkStream").value_or(nullptr);
  REQUIRE(parent != nullptr);
  REQUIRE(child != nullptr);
  REQUIRE(sink != nullptr);

  // Attach standard streams to parent
  parent->AttachStandardStreams(sink);

  // Handles 0, 1, 2 must exist
  auto h0 = parent->handles.Lookup(static_cast<kernel::KHandle>(0));
  auto h1 = parent->handles.Lookup(static_cast<kernel::KHandle>(1));
  auto h2 = parent->handles.Lookup(static_cast<kernel::KHandle>(2));

  REQUIRE(h0 != nullptr);
  REQUIRE(h1 != nullptr);
  REQUIRE(h2 != nullptr);

  REQUIRE(h0->object.get() == sink.get());
  REQUIRE(h1->object.get() == sink.get());
  REQUIRE(h2->object.get() == sink.get());

  REQUIRE(static_cast<uint32_t>(h0->access) == static_cast<uint32_t>(kernel::ProcessHandleTable::HandleAccess::Read));
  REQUIRE(static_cast<uint32_t>(h1->access) == static_cast<uint32_t>(kernel::ProcessHandleTable::HandleAccess::Write));
  REQUIRE(static_cast<uint32_t>(h2->access) == static_cast<uint32_t>(kernel::ProcessHandleTable::HandleAccess::Write));

  // Child inherits from parent
  child->InheritHandlesFrom(parent->handles);

  auto ch0 = child->handles.Lookup(static_cast<kernel::KHandle>(0));
  auto ch1 = child->handles.Lookup(static_cast<kernel::KHandle>(1));
  auto ch2 = child->handles.Lookup(static_cast<kernel::KHandle>(2));

  REQUIRE(ch0 != nullptr);
  REQUIRE(ch1 != nullptr);
  REQUIRE(ch2 != nullptr);

  REQUIRE(ch0->object.get() == sink.get());
  REQUIRE(ch1->object.get() == sink.get());
  REQUIRE(ch2->object.get() == sink.get());
}

TEST_CASE("Handle permissions and shared stream lifetime", "[tty][handles]") {
  EnsureObjectManagerInitialized();

  auto parent = kernel::CreateKObject<kernel::KProcessObject>("AccessParent").value_or(nullptr);
  auto child = kernel::CreateKObject<kernel::KProcessObject>("AccessChild").value_or(nullptr);
  auto stream = kernel::CreateAnonymousKObject<kernel::KIOObject>().value_or(nullptr);
  REQUIRE(parent != nullptr);
  REQUIRE(child != nullptr);
  REQUIRE(stream != nullptr);

  const auto read = parent->handles.Allocate(stream, kernel::ProcessHandleTable::HandleAccess::Read,
                                              kernel::ProcessHandleTable::HandleFlags::Inheritable).value();
  const auto write = parent->handles.Allocate(stream, kernel::ProcessHandleTable::HandleAccess::Write,
                                               kernel::ProcessHandleTable::HandleFlags::None).value();
  REQUIRE(parent->handles.HasAccess(read, kernel::ProcessHandleTable::HandleAccess::Read));
  REQUIRE_FALSE(parent->handles.HasAccess(read, kernel::ProcessHandleTable::HandleAccess::Write));
  REQUIRE(parent->handles.HasAccess(write, kernel::ProcessHandleTable::HandleAccess::Write));
  REQUIRE_FALSE(parent->handles.HasAccess(write, kernel::ProcessHandleTable::HandleAccess::Read));

  child->InheritHandlesFrom(parent->handles);
  REQUIRE(child->handles.HasAccess(static_cast<kernel::KHandle>(0), kernel::ProcessHandleTable::HandleAccess::Read));
  REQUIRE_FALSE(child->handles.HasAccess(static_cast<kernel::KHandle>(0), kernel::ProcessHandleTable::HandleAccess::Write));
  REQUIRE(child->handles.Lookup(static_cast<kernel::KHandle>(1)) == nullptr);

  REQUIRE(parent->handles.Close(read) == kernel::KernelError::Success);
  REQUIRE_FALSE(parent->handles.HasAccess(read, kernel::ProcessHandleTable::HandleAccess::Read));
  REQUIRE_FALSE(stream->isClosed);
  REQUIRE(parent->handles.Close(write) == kernel::KernelError::Success);
  REQUIRE_FALSE(stream->isClosed);
  REQUIRE(child->handles.Close(static_cast<kernel::KHandle>(0)) == kernel::KernelError::Success);

  const auto protectedHandle = parent->handles.Allocate(stream, kernel::ProcessHandleTable::HandleAccess::Read,
      static_cast<kernel::ProcessHandleTable::HandleFlags>(
          static_cast<uint32_t>(kernel::ProcessHandleTable::HandleFlags::Inheritable) |
          static_cast<uint32_t>(kernel::ProcessHandleTable::HandleFlags::ProtectFromClose))).value();
  REQUIRE(parent->handles.Close(protectedHandle) != kernel::KernelError::Success);
  REQUIRE(parent->handles.Lookup(protectedHandle) != nullptr);
}

namespace {

struct MockDeviceExtension {
  char rxBuffer[64]{};
  size_t rxLen{0};
  char txBuffer[64]{};
  size_t txLen{0};
};

static kernel::KernelError MockRead(const kernel::KDevicePtr<kernel::KDevice> &device,
                                 uint64_t,
                                 kstd::span<uint8_t> buffer,
                                 size_t &transferred) {
  transferred = 0;
  auto *ext = device->deviceExtension.as<MockDeviceExtension>();
  if (!ext || ext->rxLen == 0) return kernel::KernelError::Success;

  const size_t toCopy = buffer.size() < ext->rxLen ? buffer.size() : ext->rxLen;
  memcpy(buffer.data(), ext->rxBuffer, toCopy);
  transferred = toCopy;
  ext->rxLen -= toCopy;
  return kernel::KernelError::Success;
}

static kernel::KernelError MockWrite(const kernel::KDevicePtr<kernel::KDevice> &device,
                                  uint64_t,
                                  kstd::span<const uint8_t> buffer,
                                  size_t &transferred) {
  transferred = 0;
  auto *ext = device->deviceExtension.as<MockDeviceExtension>();
  if (!ext) return kernel::KernelError::DeviceError;

  const size_t space = sizeof(ext->txBuffer) - ext->txLen;
  const size_t toCopy = buffer.size() < space ? buffer.size() : space;
  memcpy(ext->txBuffer + ext->txLen, buffer.data(), toCopy);
  ext->txLen += toCopy;
  transferred = toCopy;
  return kernel::KernelError::Success;
}

} // namespace

TEST_CASE("Generic I/O device linking and pumping with KVtty", "[tty][device]") {
  EnsureObjectManagerInitialized();

  static uint32_t s_mockDevCounter = 0;
  char driverName[32];
  char devName[32];
  snprintf(driverName, sizeof(driverName), "MockDriver%u", ++s_mockDevCounter);
  snprintf(devName, sizeof(devName), "MockIoDevice%u", s_mockDevCounter);

  auto mockDriver = kernel::CreateKObject<kernel::KDriverObject>(driverName).value_or(nullptr);
  REQUIRE(mockDriver != nullptr);
  mockDriver->Read = MockRead;
  mockDriver->Write = MockWrite;

  auto mockDev = KE_IO_CreateDeviceWithContext<MockDeviceExtension>(
      mockDriver, devName, kernel::device_type::Serial, CreateDeviceFlags::None).value_or(nullptr);
  REQUIRE(mockDev != nullptr);

  kernel::tty::VttyPtr vtty(new kernel::tty::KVtty());
  REQUIRE(vtty != nullptr);

  // Attach generic device to VTTY
  REQUIRE(KE_TTY_AttachDevice(vtty, mockDev) == kernel::KernelError::Success);
  REQUIRE(KE_TTY_GetAttachedDevice(vtty) == mockDev);
  REQUIRE(KE_TTY_GetDeviceVtty(mockDev) == vtty);

  // 1. Data produced by mock device should pump into VTTY source input and become readable by sink
  auto *ext = mockDev->deviceExtension.as<MockDeviceExtension>();
  const char *msgFromDev = "HelloFromDevice\n";
  size_t msgLen = strlen(msgFromDev);
  memcpy(ext->rxBuffer, msgFromDev, msgLen);
  ext->rxLen = msgLen;

  size_t pumped = KE_TTY_PumpDevice(vtty);
  REQUIRE(pumped > 0);

  uint8_t sinkBuf[32] = {0};
  size_t nRead = vtty->sink_read(sinkBuf, sizeof(sinkBuf));
  REQUIRE(nRead == msgLen);
  REQUIRE(memcmp(sinkBuf, msgFromDev, msgLen) == 0);

  // 2. Data written by sink should pump into mock device
  ext->txLen = 0;
  memset(ext->txBuffer, 0, sizeof(ext->txBuffer));
  const uint8_t msgToDev[] = "OutToDevice\n";
  constexpr size_t toDevLen = sizeof(msgToDev) - 1;
  vtty->set_flags(kernel::tty::TTY_CANONICAL); // Without NL_TO_CRNL for exact byte match
  REQUIRE(vtty->sink_write(msgToDev, toDevLen).value() == toDevLen);

  KE_TTY_PumpDevice(vtty);
  REQUIRE(ext->txLen == toDevLen);
  REQUIRE(memcmp(ext->txBuffer, msgToDev, toDevLen) == 0);

  // Detach device
  KE_TTY_DetachDevice(vtty);
  REQUIRE(KE_TTY_GetAttachedDevice(vtty) == nullptr);
}
