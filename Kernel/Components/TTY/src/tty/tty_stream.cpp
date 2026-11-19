// TTY endpoints exposed as plain KIOObject streams. IO knows nothing about
// TTYs: the factories below fill in KIOOps and the object adapter is
// registered with IO, so the dependency only goes TTY -> IO.

#include "stdkrn.hpp"
#include <kernel/io/adapters.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/tty/tty.hpp>
#include <kernel/tty/tty_device.hpp>

namespace kernel {
namespace {
kstd::error_code TtySinkRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  KE_TTY_PumpDevice(io.vtty);
  if (io.blockingRead) {
    if (const auto status = io.vtty->sink_read_wait(buffer.data(), buffer.size(), transferred); status) {
      transferred = 0;
      return status;
    }
  } else {
    transferred = io.vtty->sink_read(buffer.data(), buffer.size());
  }
  io.offset += transferred;
  return {};
}

kstd::error_code TtySinkWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  const auto written = io.vtty->sink_write(buffer.data(), buffer.size());
  if (!written) return written.error();
  transferred = *written;
  KE_TTY_PumpDevice(io.vtty);
  io.offset += transferred;
  return {};
}

kstd::error_code TtySinkClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty && io.ownsTtyEndpoint) io.vtty->close_sink();
  return {};
}

kstd::error_code TtySourceRead(KIOObject &io, kstd::span<uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  transferred = io.vtty->source_read(buffer.data(), buffer.size());
  io.offset += transferred;
  return {};
}

kstd::error_code TtySourceWrite(KIOObject &io, kstd::span<const uint8_t> buffer, size_t &transferred) {
  if (!io.vtty) return KernelError::InvalidArgument;
  const auto written = io.vtty->source_write(buffer.data(), buffer.size());
  if (!written) return written.error();
  transferred = *written;
  io.offset += transferred;
  return {};
}

kstd::error_code TtySourceClose(KIOObject &io) {
  io.isClosed = true;
  if (io.vtty) io.vtty->close_source();
  return {};
}

KIOObjectResult CreateTtyStream(const tty::VttyPtr &vtty, const kstd::string_view &name, const KIOOps &ops) noexcept {
  if (!vtty) return kstd::unexpected(KernelError::InvalidArgument);
  auto created = name.empty()
                     ? CreateAnonymousKObject<KIOObject>()
                     : CreateKObject<KIOObject>(name);
  if (!created) return kstd::unexpected(created.error());
  auto ioObj = kstd::move(*created);

  ioObj->ops = &ops;
  ioObj->vtty = vtty;
  ioObj->mode = KIOObject::OpenMode::ReadWrite;
  return ioObj;
}

KIOObjectResult AdaptTtyEndpoint(const KObjectPtr<KObject> &obj) noexcept {
  if (const auto sink = obj.As<KVttySinkObject>()) {
    return KE_IO_CreateTtySinkStream(sink->vtty, sink->name);
  }
  if (const auto source = obj.As<KVttySourceObject>()) {
    return KE_IO_CreateTtySourceStream(source->vtty, source->name);
  }
  return kstd::unexpected(KernelError::WrongObjectType);
}

// Global constructor (Docs/KernelGlobalConstructors.md): only touches IO's
// constant-initialised adapter table.
struct TtyAdapterRegistrar {
  TtyAdapterRegistrar() noexcept { (void) io::RegisterObjectAdapter(AdaptTtyEndpoint); }
} s_ttyAdapterRegistrar;
}// namespace

namespace io {
const KIOOps TtySinkOps{.Read = TtySinkRead, .Write = TtySinkWrite, .Control = {}, .Close = TtySinkClose};
const KIOOps TtySourceOps{.Read = TtySourceRead, .Write = TtySourceWrite, .Control = {}, .Close = TtySourceClose};
}// namespace io

}// namespace kernel

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kstd::error_code> KE_IO_CreateTtySinkStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  return kernel::CreateTtyStream(vtty, name, kernel::io::TtySinkOps);
}

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KIOObject>, kstd::error_code> KE_IO_CreateTtySourceStream(
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name) noexcept {
  return kernel::CreateTtyStream(vtty, name, kernel::io::TtySourceOps);
}
