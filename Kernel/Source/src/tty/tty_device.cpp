#include "stdkrn.hpp"
#include <kernel/device.hpp>
#include <kernel/io.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/mutex.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/tty/tty_device.hpp>
#include <mutex.hpp>

namespace {
constexpr size_t MAX_ATTACHED_DEVICES = 16;

struct AttachedPair {
  kernel::tty::VttyPtr vtty{nullptr};
  kernel::KDevicePtr<kernel::KDevice> device{nullptr};
};

AttachedPair s_attachedPairs[MAX_ATTACHED_DEVICES];
kernel::Mutex s_attachLock;
kernel::Mutex s_pumpLock;
kernel::KDispatcherHeader s_pumpWake{.manualReset = false};

void tty_pump_thread(void *) {
  for (;;) {
    KE_TTY_PumpAllAttachedDevices();
    const auto status = KE_SCHED_WaitForHeader(s_pumpWake, kstd::chrono::milliseconds(10));
    if (status && status != kernel::KernelError::Timeout) {
      KE_Print("TTY pump wait failed: {}", status.message());
      HAL_PLATFORM_Panic("TTY pump wait failed", __FILE__, __LINE__);
    }
  }
}
}// namespace

namespace kernel::tty::detail {
kstd::error_code StartPumpThread() noexcept {
  const auto worker = KE_SCHED_CreateThread(
      kernel::cfunc<void(void *)>(&tty_pump_thread), nullptr, 1);
  return worker ? kernel::KernelError::Success : worker.error();
}
}// namespace kernel::tty::detail

UNDOS_KERNEL_API_DEF kstd::expected<kernel::tty::TtyPair, kstd::error_code> KE_TTY_CreatePair(
    const kstd::string_view &namePrefix) noexcept {

  kernel::tty::TtyPair pair;
  pair.vtty = kernel::tty::VttyPtr(new kernel::tty::KVtty());
  if (!pair.vtty) return kstd::unexpected(kernel::KernelError::OutOfMemory);

  kstd::static_string<64> sourceName;
  kstd::static_string<64> sinkName;
  kstd::format(sourceName, "{}Source", namePrefix);
  kstd::format(sinkName, "{}Sink", namePrefix);

  auto source = kernel::CreateKObject<kernel::KVttySourceObject>(sourceName);
  if (!source) return kstd::unexpected(source.error());
  auto sink = kernel::CreateKObject<kernel::KVttySinkObject>(sinkName);
  if (!sink) return kstd::unexpected(sink.error());

  pair.source = kstd::move(*source);
  pair.sink = kstd::move(*sink);
  pair.source->vtty = pair.vtty;
  pair.sink->vtty = pair.vtty;
  return pair;
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_TTY_AttachDevice(const kernel::tty::VttyPtr &vtty,
                                                             const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!vtty || !device) return kernel::KernelError::InvalidArgument;

  bool attached = false;
  {
    kstd::unique_lock lock(s_attachLock);

    // Check if already attached
    for (auto &s_attachedPair: s_attachedPairs) {
      if (s_attachedPair.vtty.get() == vtty.get()) {
        s_attachedPair.device = device;
        attached = true;
        break;
      }
    }

    // Find free slot
    if (!attached) {
      for (auto &s_attachedPair: s_attachedPairs) {
        if (s_attachedPair.vtty == nullptr) {
          s_attachedPair.vtty = vtty;
          s_attachedPair.device = device;
          attached = true;
          break;
        }
      }
    }
  }

  if (!attached) return kernel::KernelError::ResourceLimitExceeded;
  KE_EVENT_Signal(s_pumpWake, false);
  return {};
}

UNDOS_KERNEL_API_DEF void KE_TTY_DetachDevice(const kernel::tty::VttyPtr &vtty) noexcept {
  if (!vtty) return;
  kstd::unique_lock lock(s_attachLock);

  for (auto &s_attachedPair: s_attachedPairs) {
    if (s_attachedPair.vtty.get() == vtty.get()) {
      s_attachedPair.vtty = nullptr;
      s_attachedPair.device = nullptr;
      break;
    }
  }
}

UNDOS_KERNEL_API_DEF kernel::KDevicePtr<kernel::KDevice> KE_TTY_GetAttachedDevice(const kernel::tty::VttyPtr &vtty) noexcept {
  if (!vtty) return nullptr;
  kstd::unique_lock lock(s_attachLock);

  for (auto &s_attachedPair: s_attachedPairs) {
    if (s_attachedPair.vtty.get() == vtty.get()) {
      return s_attachedPair.device;
    }
  }
  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::tty::VttyPtr KE_TTY_GetDeviceVtty(const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!device) return nullptr;
  kstd::unique_lock lock(s_attachLock);

  for (auto &s_attachedPair: s_attachedPairs) {
    if (s_attachedPair.device.get() == device.get()) {
      return s_attachedPair.vtty;
    }
  }
  return nullptr;
}

namespace {
size_t DrainSourceToDevice(const kernel::tty::VttyPtr &vtty, const kernel::KDevicePtr<kernel::KDevice> &device) {
  uint8_t txBuf[128];
  size_t total = 0;
  while (vtty->source_has_data()) {
    const size_t nRead = vtty->source_read(txBuf, sizeof(txBuf));
    if (nRead == 0) break;

    size_t written = 0;
    if (const auto status = KE_IO_WriteDevice(device, 0, kstd::span<const uint8_t>(txBuf, nRead), written);
        status || written == 0) {
      break;
    }
    total += written;
  }
  return total;
}
}// namespace

UNDOS_KERNEL_API_DEF size_t KE_TTY_PumpDevice(const kernel::tty::VttyPtr &vtty) noexcept {
  if (!vtty) return 0;
  kstd::unique_lock pumpLock(s_pumpLock);
  const auto device = KE_TTY_GetAttachedDevice(vtty);
  if (!device) return 0;

  size_t totalActivity = 0;

  totalActivity += DrainSourceToDevice(vtty, device);

  // 2. Poll device input into source
  uint8_t rxBuf[128];
  size_t readBytes = 0;
  if (const auto status = KE_IO_ReadDevice(device, 0, kstd::span(rxBuf, sizeof(rxBuf)), readBytes);
      !status && readBytes > 0) {
    // A closed endpoint just drops the input.
    (void) vtty->source_write(rxBuf, readBytes);
    totalActivity += readBytes;

    totalActivity += DrainSourceToDevice(vtty, device);
  }

  return totalActivity;
}

UNDOS_KERNEL_API_DEF void KE_TTY_PumpAllAttachedDevices() noexcept {
  kernel::tty::VttyPtr activeVttys[MAX_ATTACHED_DEVICES];
  size_t count = 0;

  {
    kstd::unique_lock lock(s_attachLock);
    for (const auto &s_attachedPair: s_attachedPairs) {
      if (s_attachedPair.vtty && s_attachedPair.device) {
        activeVttys[count++] = s_attachedPair.vtty;
      }
    }
  }

  for (size_t i = 0; i < count; ++i) {
    KE_TTY_PumpDevice(activeVttys[i]);
  }
}

