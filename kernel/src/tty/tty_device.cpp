#include "stdkrn.hpp"
#include <kernel/device.hpp>
#include <kernel/io.hpp>
#include <kernel/mutex.hpp>
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
kernel::KObjectPtr<kernel::KVttySinkObject> s_defaultConsoleSink{nullptr};
}// namespace

UNDOS_KERNEL_API_DEF bool KE_TTY_CreatePair(
    kernel::tty::VttyPtr &outVtty,
    kernel::KObjectPtr<kernel::KVttySourceObject> &outSource,
    kernel::KObjectPtr<kernel::KVttySinkObject> &outSink,
    const kstd::string_view &namePrefix) noexcept {

  outVtty = kernel::tty::VttyPtr(new kernel::tty::KVtty());
  if (!outVtty) return false;

  kstd::static_string<64> sourceName;
  kstd::static_string<64> sinkName;
  kstd::format(sourceName, "{}Source", namePrefix);
  kstd::format(sinkName, "{}Sink", namePrefix);

  outSource = kernel::CreateKObject<kernel::KVttySourceObject>(sourceName);
  outSink = kernel::CreateKObject<kernel::KVttySinkObject>(sinkName);

  if (!outSource || !outSink) {
    outSource = nullptr;
    outSink = nullptr;
    outVtty = nullptr;
    return false;
  }

  outSource->vtty = outVtty;
  outSink->vtty = outVtty;
  return true;
}

UNDOS_KERNEL_API_DEF bool KE_TTY_AttachDevice(const kernel::tty::VttyPtr &vtty,
                                              const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!vtty || !device) return false;
  kstd::unique_lock lock(s_attachLock);

  // Check if already attached
  for (auto &s_attachedPair: s_attachedPairs) {
    if (s_attachedPair.vtty.get() == vtty.get()) {
      s_attachedPair.device = device;
      return true;
    }
  }

  // Find free slot
  for (auto &s_attachedPair: s_attachedPairs) {
    if (s_attachedPair.vtty == nullptr) {
      s_attachedPair.vtty = vtty;
      s_attachedPair.device = device;
      return true;
    }
  }

  return false;
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

UNDOS_KERNEL_API_DEF size_t KE_TTY_PumpDevice(const kernel::tty::VttyPtr &vtty) noexcept {
  if (!vtty) return 0;
  const auto device = KE_TTY_GetAttachedDevice(vtty);
  if (!device) return 0;

  size_t totalActivity = 0;

  // 1. Drain source output ring to the device
  uint8_t txBuf[128];
  while (vtty->source_has_data()) {
    constexpr size_t toRead = sizeof(txBuf);
    const size_t nRead = vtty->source_read(txBuf, toRead);
    if (nRead == 0) break;

    size_t written = 0;
    if (const auto status = KE_IO_WriteDevice(device, 0, kstd::span<const uint8_t>(txBuf, nRead), written);
      status != kernel::IoStatus::Success || written == 0) {
      break;
    }
    totalActivity += written;
  }

  // 2. Poll device input into source
  uint8_t rxBuf[128];
  size_t readBytes = 0;
  if (const auto status = KE_IO_ReadDevice(device, 0, kstd::span(rxBuf, sizeof(rxBuf)), readBytes);
      status == kernel::IoStatus::Success && readBytes > 0) {
    vtty->source_write(rxBuf, readBytes);
    totalActivity += readBytes;

    // Immediately drain any echo produced by source_write back to the device
    while (vtty->source_has_data()) {
      constexpr size_t toRead = sizeof(txBuf);
      const size_t nRead = vtty->source_read(txBuf, toRead);
      if (nRead == 0) break;

      size_t written = 0;
      if (const auto st = KE_IO_WriteDevice(device, 0, kstd::span<const uint8_t>(txBuf, nRead), written);
          st != kernel::IoStatus::Success || written == 0) {
        break;
      }
      totalActivity += written;
    }
  }

  return totalActivity;
}

UNDOS_KERNEL_API_DEF void KE_TTY_PumpAllAttachedDevices() noexcept {
  kernel::tty::VttyPtr activeVttys[MAX_ATTACHED_DEVICES];
  size_t count = 0;

  {
    kstd::unique_lock lock(s_attachLock);
    for (auto &s_attachedPair: s_attachedPairs) {
      if (s_attachedPair.vtty && s_attachedPair.device) {
        activeVttys[count++] = s_attachedPair.vtty;
      }
    }
  }

  for (size_t i = 0; i < count; ++i) {
    KE_TTY_PumpDevice(activeVttys[i]);
  }
}

UNDOS_KERNEL_API_DEF void KE_TTY_SetDefaultConsole(const kernel::KObjectPtr<kernel::KVttySinkObject> &sink) noexcept {
  kstd::unique_lock lock(s_attachLock);
  s_defaultConsoleSink = sink;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KVttySinkObject> KE_TTY_GetDefaultConsoleSink() noexcept {
  kstd::unique_lock lock(s_attachLock);
  return s_defaultConsoleSink;
}
