
#include <Kernel.hpp>
#include <kernel/input.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/resource.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

#include <span.hpp>

#include "gameport_hid_report.hpp"
#include "gameport_registers.hpp"
#include "hid_parser.hpp"

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlGamepadCaps.h>
#include <undos/syscalls/__struct_IoctlGamepadState.h>

// Analog IBM PC gameport driver. The port has no interrupt and its sample is
// a timing-sensitive wait on an RC discharge, which is exactly the work that
// must never run in interrupt context -- so everything here happens on a
// dedicated kernel thread that sleeps between samples, keeping
// KE_INPUT_PushEvent passive level for this driver too.
namespace {

// Per-device state (no driver globals, per AGENTS.md).
struct GameportContext {
  uint16_t ioBase = gameport_regs::kDefaultIoBase;

  // Observed maximum discharge count per axis, used to normalize. Seeded
  // from the standard ~100k-ohm timing constant rather than left at zero, so
  // early samples under-range instead of pinning to an extreme; widened
  // whenever a larger count is seen.
  uint16_t axisMax[gameport_regs::kAxisCount] = {};
  bool axisPresent[gameport_regs::kAxisCount] = {};

  kernel::hid::ReportFormat format;
  kernel::hid::DecoderState hidState;

  kernel::KDevicePtr<kernel::KDevice> device{nullptr};
  kernel::KObjectPtr<kernel::KThreadObject> pollThread{nullptr};

  // Never signalled: waited on purely as a bounded sleep between polls,
  // since the scheduler exposes no sleep primitive of its own.
  kernel::KDispatcherHeader idle{};
};

// A typical gameport axis settles well inside a millisecond; seeding the
// normalization range here means the very first sample is already roughly
// scaled rather than reading as full deflection.
constexpr uint16_t kSeedAxisMax = 1000;

// Hard ceiling on one sample. The read is bounded against the system clock
// rather than an instruction count, so it behaves the same on a 386 and on a
// modern host, and on the non-x86 targets.
constexpr kstd::chrono::nanoseconds kSampleTimeout{2'000'000};// 2 ms

constexpr kstd::chrono::milliseconds kPollInterval{16};// ~60 Hz

// Takes one sample: start the discharge, then count iterations until each
// axis bit clears, giving up against the system clock. An axis whose bit
// never clears is reported absent rather than at an extreme.
gameport::Sample ReadSample(uint16_t ioBase) noexcept {
  gameport::Sample sample;

  HAL_IO_Out8(ioBase, 0xFF);// any write starts the RC discharge
  const kstd::chrono::nanoseconds start = KE_TIME_GetSystemTime();

  uint8_t pending = gameport_regs::status::AxisMask;
  uint16_t ticks = 0;
  uint8_t last = 0;
  while (pending != 0) {
    last = HAL_IO_In8(ioBase);
    ++ticks;
    for (size_t i = 0; i < gameport_regs::kAxisCount; ++i) {
      const uint8_t bit = static_cast<uint8_t>(1u << i);
      if ((pending & bit) == 0) continue;
      if (last & bit) continue;
      sample.counts[i] = ticks;
      sample.axisValid[i] = true;
      pending = static_cast<uint8_t>(pending & ~bit);
    }
    if (KE_TIME_GetSystemTime() - start > kSampleTimeout) break;
  }

  sample.rawButtons = last;
  return sample;
}

// Presence detection. With no joystick attached the port typically reads
// back all-ones or all-zeros and the axis bits never clear; either is
// treated as "absent" so no device is published for a port that would only
// ever report motionless axes.
bool DetectJoystick(uint16_t ioBase) noexcept {
  const uint8_t idle = HAL_IO_In8(ioBase);
  if (idle == 0xFF || idle == 0x00) {
    // Could still be a port mid-discharge; take one real sample to be sure.
    const gameport::Sample sample = ReadSample(ioBase);
    for (const bool valid: sample.axisValid) {
      if (valid) return true;
    }
    return false;
  }
  return true;
}

void Gameport_PollThread(void *arg) {
  auto *ctx = static_cast<GameportContext *>(arg);
  if (ctx == nullptr) return;

  for (;;) {
    const gameport::Sample sample = ReadSample(ctx->ioBase);

    // A sample where no axis completed means the read timed out; keep the
    // previous snapshot rather than publishing a bogus one.
    bool anyAxis = false;
    for (size_t i = 0; i < gameport_regs::kAxisCount; ++i) {
      if (!sample.axisValid[i]) continue;
      anyAxis = true;
      ctx->axisPresent[i] = true;
    }

    if (anyAxis) {
      uint8_t report[gameport::kReportBytes]{};
      gameport::PackJoystickReport(sample, ctx->axisMax, report);
      kernel::hid::SubmitReport(ctx->device, ctx->format, ctx->hidState,
                                kstd::span<const uint8_t>(report, gameport::kReportBytes));
    }

    (void) KE_SCHED_WaitForHeader(ctx->idle, kPollInterval);
  }
}

kernel::KernelError Gameport_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                        kstd::span<const uint8_t> input, kstd::span<uint8_t> output,
                                        size_t &transferred) {
  (void) input;
  transferred = 0;

  auto *ctx = device ? device->deviceExtension.as<GameportContext>() : nullptr;
  if (ctx == nullptr) return kernel::KernelError::Unsupported;

  switch (code) {
    case IoControlCode_GamepadReadState: {
      if (output.size() < sizeof(IoctlGamepadState)) return kernel::KernelError::InvalidSize;
      const auto gamepad = KE_INPUT_GetGamepadState(device);
      if (!gamepad) return gamepad.error();
      const auto &state = *gamepad;

      IoctlGamepadState out{};
      out.leftX = state.axes[kernel::input::gamepad_axis::LeftX];
      out.leftY = state.axes[kernel::input::gamepad_axis::LeftY];
      out.rightX = state.axes[kernel::input::gamepad_axis::RightX];
      out.rightY = state.axes[kernel::input::gamepad_axis::RightY];
      out.leftTrigger = state.axes[kernel::input::gamepad_axis::LeftTrigger];
      out.rightTrigger = state.axes[kernel::input::gamepad_axis::RightTrigger];
      out.buttons = state.buttons;
      out.hatX = state.hatX;
      out.hatY = state.hatY;
      out.sequence = state.sequence;

      __builtin_memcpy(output.data(), &out, sizeof(out));
      transferred = sizeof(out);
      return kernel::KernelError::Success;
    }
    case IoControlCode_GamepadQueryCaps: {
      if (output.size() < sizeof(IoctlGamepadCaps)) return kernel::KernelError::InvalidSize;

      IoctlGamepadCaps caps{};
      for (const bool present: ctx->axisPresent) {
        if (present) ++caps.axisCount;
      }
      caps.buttonCount = gameport_regs::kButtonCount;
      caps.hasHat = 0;// an analog gameport has no POV hat

      __builtin_memcpy(output.data(), &caps, sizeof(caps));
      transferred = sizeof(caps);
      return kernel::KernelError::Success;
    }
    default:
      return kernel::KernelError::Unsupported;
  }
}

kernel::KernelError Gameport_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver,
                        const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateBusWithContext<GameportContext>(driver, {});
  if (!fdo) {
    KE_Print("gameport: Failed to create FDO");
    return fdo.error();
  }

  const auto attached = KE_IO_AttachFilterDevice(*fdo, pdo);
  if (!attached) return attached.error();
  return kernel::KernelError::Success;
}

void Gameport_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!fdo) {
    KE_Print("gameport: No FDO attached; cannot start");
    return;
  }

  auto *fdoCtx = fdo->deviceExtension.as<GameportContext>();
  if (fdoCtx == nullptr) {
    KE_Print("gameport: Missing GameportContext in deviceExtension");
    return;
  }

  uint16_t ioBase = gameport_regs::kDefaultIoBase;
  if (const auto *ioRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 0)) {
    ioBase = static_cast<uint16_t>(ioRes->start);
  }
  fdoCtx->ioBase = ioBase;

  if (!DetectJoystick(ioBase)) {
    KE_Print("gameport: no joystick detected at 0x{x}; no device created", ioBase);
    return;
  }

  const auto createdDevice = KE_IO_CreateDeviceWithContext<GameportContext>(
      fdo->driverObject, "Gamepad", kernel::device_type::Gamepad,
      CreateDeviceFlags::MultipleAllowed);
  if (!createdDevice) {
    KE_Print("gameport: Failed to create Gamepad device");
    return;
  }
  const auto &device = *createdDevice;

  auto *ctx = device->deviceExtension.as<GameportContext>();
  if (ctx == nullptr) return;

  ctx->ioBase = ioBase;
  for (auto &max: ctx->axisMax) max = kSeedAxisMax;
  ctx->device = device;

  if (kernel::hid::ParseReportDescriptor(
          kstd::span<const uint8_t>(gameport::kJoystickReportDescriptor,
                                    sizeof(gameport::kJoystickReportDescriptor)),
          ctx->format) != kernel::KernelError::Success) {
    KE_Print("gameport: joystick HID descriptor rejected; no device created");
    return;
  }

  device->hardwareId = "input_gameport";
  if (KE_IO_AttachChildDevice(fdo, device) != kernel::KernelError::Success) {
    KE_Print("gameport: Failed to attach Gamepad device");
    return;
  }

  ctx->idle.manualReset = false;
  ctx->idle.signalled = false;
  auto thread = KE_SCHED_CreateThread(&Gameport_PollThread, ctx, 0);
  if (!thread) {
    KE_Print("gameport: Failed to create poll thread; joystick inactive");
    return;
  }
  ctx->pollThread = kstd::move(*thread);

  KE_Print("gameport: analog joystick at 0x{x} polling at ~60 Hz", ioBase);
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = Gameport_AddDevice;
  driver->StartDevice = Gameport_StartDevice;
  driver->DeviceControl = Gameport_DeviceControl;
}
