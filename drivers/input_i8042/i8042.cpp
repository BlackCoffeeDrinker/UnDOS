
#include <Kernel.hpp>
#include <kernel/input.hpp>
#include <kernel/resource.hpp>

#include "hid_parser.hpp"
#include "i8042_context.hpp"
#include "i8042_registers.hpp"
#include "ps2_pump.hpp"

#include <span.hpp>

#include <undos/syscalls/__enum_IoControlCode.h>
#include <undos/syscalls/__struct_IoctlMouseConfig.h>

namespace {

// Bounded-spin polled I/O primitives. A real 8042 (and QEMU's model) always
// clears IBF/sets OBF within a handful of I/O accesses; a stuck bit means a
// bad/missing controller, which must be logged and handled gracefully --
// never an unbounded loop that hangs boot.
constexpr int kSpinLimit = 100000;

bool I8042_WaitInputEmpty() {
  for (int i = 0; i < kSpinLimit; ++i) {
    if (!(HAL_IO_In8(i8042_regs::kStatusPort) & i8042_regs::status::InputBufferFull)) return true;
  }
  return false;
}

bool I8042_WaitOutputFull() {
  for (int i = 0; i < kSpinLimit; ++i) {
    if (HAL_IO_In8(i8042_regs::kStatusPort) & i8042_regs::status::OutputBufferFull) return true;
  }
  return false;
}

bool I8042_ReadData(uint8_t &out) {
  if (!I8042_WaitOutputFull()) return false;
  out = HAL_IO_In8(i8042_regs::kDataPort);
  return true;
}

bool I8042_WriteCommand(uint8_t cmd) {
  if (!I8042_WaitInputEmpty()) return false;
  HAL_IO_Out8(i8042_regs::kCommandPort, cmd);
  return true;
}

bool I8042_WriteData(uint8_t data) {
  if (!I8042_WaitInputEmpty()) return false;
  HAL_IO_Out8(i8042_regs::kDataPort, data);
  return true;
}

// Drains any stale byte(s) left in the output buffer by firmware/BIOS at
// boot. Bounded by a fixed small iteration count rather than kSpinLimit,
// since an empty buffer is the expected steady state and each iteration
// only costs a status read.
void I8042_FlushOutputBuffer() {
  for (int i = 0; i < 16; ++i) {
    if (!(HAL_IO_In8(i8042_regs::kStatusPort) & i8042_regs::status::OutputBufferFull)) break;
    (void) HAL_IO_In8(i8042_regs::kDataPort);
  }
}

// Runs the controller self-test, the keyboard interface test, programs the
// command byte (keyboard IRQ enabled, set-1 translation, keyboard clock
// enabled) and resets the keyboard. Every failure path logs and returns
// false rather than panicking, so a bad/missing controller cannot break
// boot; the aux port is left disabled here and enabled starting Task 14.
bool I8042_InitController() {
  if (!I8042_WriteCommand(i8042_regs::command::DisableKeyboardPort)) {
    KE_Print("i8042: WriteCommand(DisableKeyboardPort) timed out");
    return false;
  }
  if (!I8042_WriteCommand(i8042_regs::command::DisableAuxPort)) {
    KE_Print("i8042: WriteCommand(DisableAuxPort) timed out");
    return false;
  }

  I8042_FlushOutputBuffer();

  if (!I8042_WriteCommand(i8042_regs::command::SelfTest)) {
    KE_Print("i8042: WriteCommand(SelfTest) timed out");
    return false;
  }
  uint8_t selfTestResult = 0;
  if (!I8042_ReadData(selfTestResult)) {
    KE_Print("i8042: self-test response timed out");
    return false;
  }
  KE_Print("i8042: self-test=0x{x}", selfTestResult);
  if (selfTestResult != 0x55) {
    KE_Print("i8042: controller self-test failed");
    return false;
  }

  if (!I8042_WriteCommand(i8042_regs::command::TestKeyboardPort)) {
    KE_Print("i8042: WriteCommand(TestKeyboardPort) timed out");
    return false;
  }
  uint8_t kbdTestResult = 0;
  if (!I8042_ReadData(kbdTestResult)) {
    KE_Print("i8042: keyboard interface test response timed out");
    return false;
  }
  KE_Print("i8042: keyboard interface test=0x{x}", kbdTestResult);
  if (kbdTestResult != 0x00) {
    KE_Print("i8042: keyboard interface test failed");
    return false;
  }

  if (!I8042_WriteCommand(i8042_regs::command::ReadCommandByte)) {
    KE_Print("i8042: WriteCommand(ReadCommandByte) timed out");
    return false;
  }
  uint8_t ccb = 0;
  if (!I8042_ReadData(ccb)) {
    KE_Print("i8042: read CCB timed out");
    return false;
  }
  KE_Print("i8042: CCB (before)=0x{x}", ccb);

  ccb |= i8042_regs::ccb::KeyboardInterruptEnable;
  ccb |= i8042_regs::ccb::KeyboardTranslation;
  ccb &= static_cast<uint8_t>(~i8042_regs::ccb::KeyboardClockDisable);

  if (!I8042_WriteCommand(i8042_regs::command::WriteCommandByte)) {
    KE_Print("i8042: WriteCommand(WriteCommandByte) timed out");
    return false;
  }
  if (!I8042_WriteData(ccb)) {
    KE_Print("i8042: write CCB timed out");
    return false;
  }
  KE_Print("i8042: CCB (after)=0x{x}", ccb);

  if (!I8042_WriteCommand(i8042_regs::command::EnableKeyboardPort)) {
    KE_Print("i8042: WriteCommand(EnableKeyboardPort) timed out");
    return false;
  }

  if (!I8042_WriteData(i8042_regs::device::Reset)) {
    KE_Print("i8042: keyboard reset write timed out");
    return false;
  }
  uint8_t ack = 0;
  if (!I8042_ReadData(ack)) {
    KE_Print("i8042: keyboard reset ack timed out");
    return false;
  }
  KE_Print("i8042: reset ack=0x{x}", ack);
  if (ack != i8042_regs::device::Ack) {
    KE_Print("i8042: keyboard reset not acknowledged");
    return false;
  }
  uint8_t bat = 0;
  if (!I8042_ReadData(bat)) {
    KE_Print("i8042: keyboard BAT timed out");
    return false;
  }
  KE_Print("i8042: BAT=0x{x}", bat);
  if (bat != i8042_regs::device::BatPass) {
    KE_Print("i8042: keyboard BAT failed");
    return false;
  }

  return true;
}

// Forwards one data byte to the aux (mouse) device via command::WriteToAuxPort
// -- every aux-directed write goes through this, per the 8042 spec.
bool I8042_WriteAuxCommand(uint8_t data) {
  if (!I8042_WriteCommand(i8042_regs::command::WriteToAuxPort)) return false;
  return I8042_WriteData(data);
}

// Reads one response byte and checks it is the device Ack (0xFA). Every
// aux command below is followed by exactly one of these before the next
// step, so a missing/wedged mouse times out here (bounded by
// I8042_ReadData's WaitOutputFull spin) rather than hanging boot.
bool I8042_WaitAck() {
  uint8_t ack = 0;
  if (!I8042_ReadData(ack)) return false;
  return ack == i8042_regs::device::Ack;
}

// Sends the 0xF3 <rate> sample-rate command, used both for Task 14's mouse
// bring-up and Task 15's Microsoft wheel/5-button knock sequences (a
// sequence of specific sample-rate writes with no other purpose than to be
// recognised by IntelliMouse-compatible firmware).
bool I8042_SetMouseSampleRate(uint8_t rate) {
  if (!I8042_WriteAuxCommand(i8042_regs::device::SetSampleRate) || !I8042_WaitAck()) return false;
  if (!I8042_WriteAuxCommand(rate) || !I8042_WaitAck()) return false;
  return true;
}

// Issues 0xF2 (get device ID) and returns the single response byte, or
// 0xFF (never a real device ID) if the command/ack/read failed -- treated
// the same as "no extension support" by the negotiation below.
uint8_t I8042_ReadMouseDeviceId() {
  if (!I8042_WriteAuxCommand(i8042_regs::device::GetDeviceId) || !I8042_WaitAck()) return 0xFF;
  uint8_t id = 0xFF;
  if (!I8042_ReadData(id)) return 0xFF;
  return id;
}

// Enables the aux port at the controller level and brings the mouse itself
// into streaming mode (defaults, sample rate, data reporting on). Every
// failure path logs and returns false without touching the CCB bits it
// hasn't set yet, leaving the keyboard -- already fully initialised by
// I8042_InitController -- completely unaffected.
bool I8042_InitMouse() {
  if (!I8042_WriteCommand(i8042_regs::command::EnableAuxPort)) {
    KE_Print("i8042: WriteCommand(EnableAuxPort) timed out");
    return false;
  }
  if (!I8042_WriteCommand(i8042_regs::command::TestAuxPort)) {
    KE_Print("i8042: WriteCommand(TestAuxPort) timed out");
    return false;
  }
  uint8_t auxTestResult = 0;
  if (!I8042_ReadData(auxTestResult)) {
    KE_Print("i8042: aux interface test response timed out; no mouse");
    return false;
  }
  KE_Print("i8042: aux interface test=0x{x}", auxTestResult);
  if (auxTestResult != 0x00) {
    KE_Print("i8042: aux interface test failed; no mouse");
    return false;
  }

  if (!I8042_WriteCommand(i8042_regs::command::ReadCommandByte)) {
    KE_Print("i8042: WriteCommand(ReadCommandByte) [aux] timed out");
    return false;
  }
  uint8_t ccb = 0;
  if (!I8042_ReadData(ccb)) {
    KE_Print("i8042: read CCB [aux] timed out");
    return false;
  }
  ccb |= i8042_regs::ccb::AuxInterruptEnable;
  ccb &= static_cast<uint8_t>(~i8042_regs::ccb::AuxClockDisable);
  if (!I8042_WriteCommand(i8042_regs::command::WriteCommandByte)) {
    KE_Print("i8042: WriteCommand(WriteCommandByte) [aux] timed out");
    return false;
  }
  if (!I8042_WriteData(ccb)) {
    KE_Print("i8042: write CCB [aux] timed out");
    return false;
  }

  if (!I8042_WriteAuxCommand(i8042_regs::device::Reset)) {
    KE_Print("i8042: mouse reset write timed out; no mouse");
    return false;
  }
  if (!I8042_WaitAck()) {
    KE_Print("i8042: mouse reset not acknowledged; no mouse");
    return false;
  }
  uint8_t bat = 0;
  if (!I8042_ReadData(bat)) {
    KE_Print("i8042: mouse BAT response timed out; no mouse");
    return false;
  }
  KE_Print("i8042: mouse BAT=0x{x}", bat);
  if (bat != i8042_regs::device::BatPass) {
    KE_Print("i8042: mouse BAT failed; no mouse");
    return false;
  }
  uint8_t deviceId = 0;
  if (!I8042_ReadData(deviceId)) {
    KE_Print("i8042: mouse device-id read timed out; no mouse");
    return false;
  }
  KE_Print("i8042: mouse device id=0x{x}", deviceId);

  if (!I8042_WriteAuxCommand(i8042_regs::device::SetDefaults) || !I8042_WaitAck()) {
    KE_Print("i8042: mouse set-defaults failed; no mouse");
    return false;
  }

  if (!I8042_SetMouseSampleRate(100)) {
    KE_Print("i8042: mouse sample-rate command failed; no mouse");
    return false;
  }

  if (!I8042_WriteAuxCommand(i8042_regs::device::EnableDataReporting) || !I8042_WaitAck()) {
    KE_Print("i8042: mouse enable-reporting failed; no mouse");
    return false;
  }

  KE_Print("i8042: mouse in streaming mode");
  return true;
}

// Microsoft's IntelliMouse wheel and IntelliMouse-Explorer 5-button "knock"
// sequences: a specific run of sample-rate writes that plain PS/2 mice
// ignore, but IntelliMouse-compatible firmware recognises as a request to
// report a wider device ID (0x03 / 0x04) and switch to 4-byte packets. Any
// other response -- including a timeout -- leaves the decoder on
// Standard3Byte, which is always safe.
//
// This runs before either ISR is connected, so the framing is settled before
// the first interrupt and the pump can never observe a half-configured
// decoder. The mode is written straight into the pointer device's own
// context -- the kernel has no opinion on PS/2 framing.
void I8042_NegotiateMouseExtensions(PointerDeviceContext &pointer) {
  if (!I8042_SetMouseSampleRate(200) || !I8042_SetMouseSampleRate(100) || !I8042_SetMouseSampleRate(80)) {
    KE_Print("i8042: mouse wheel knock sequence failed; mouse protocol = Standard3Byte");
    return;
  }
  const uint8_t wheelId = I8042_ReadMouseDeviceId();
  KE_Print("i8042: mouse device id after wheel knock=0x{x}", wheelId);
  if (wheelId != 0x03) {
    KE_Print("i8042: mouse protocol = Standard3Byte");
    return;
  }
  pointer.decoder.SetMode(ps2::MousePacketMode::IntelliMouse4Byte);

  if (!I8042_SetMouseSampleRate(200) || !I8042_SetMouseSampleRate(200) || !I8042_SetMouseSampleRate(80)) {
    KE_Print("i8042: mouse 5-button knock sequence failed; mouse protocol = IntelliMouse4Byte");
    return;
  }
  const uint8_t explorerId = I8042_ReadMouseDeviceId();
  KE_Print("i8042: mouse device id after 5-button knock=0x{x}", explorerId);
  if (explorerId == 0x04) {
    pointer.decoder.SetMode(ps2::MousePacketMode::IntelliMouseExplorer4Byte);
    KE_Print("i8042: mouse protocol = IntelliMouseExplorer4Byte");
  } else {
    KE_Print("i8042: mouse protocol = IntelliMouse4Byte");
  }
}

// INTERRUPT CONTEXT. Drains the controller's output buffer into the owning
// device's byte ring and wakes the pump; the same draining logic serves both
// the keyboard (IRQ1) and aux (IRQ12) ISRs, since hardware tags the byte's
// origin in the status register regardless of which line raised the
// interrupt. Bounded loop, port I/O and a ring push only: no decoding, no
// HID symbol, no kernel input call, no allocation, no KE_Print. A byte that
// cannot be buffered is counted and dropped rather than making the ISR
// block or spin.
template<typename ContextT>
bool I8042_PushByte(const kernel::KDevicePtr<kernel::KDevice> &device, uint8_t data) noexcept {
  if (!device) return false;
  auto *ctx = device->deviceExtension.as<ContextT>();
  if (ctx == nullptr) return false;
  if (!ctx->bytes.Push(data)) {
    ++ctx->droppedBytes;
    return false;
  }
  return true;
}

void I8042_DrainToRings(I8042Context *ctx) noexcept {
  if (ctx == nullptr) return;

  bool buffered = false;
  for (int i = 0; i < 16; ++i) {
    const uint8_t status = HAL_IO_In8(i8042_regs::kStatusPort);
    if (!(status & i8042_regs::status::OutputBufferFull)) break;

    const uint8_t data = HAL_IO_In8(i8042_regs::kDataPort);
    if (status & i8042_regs::status::AuxData) {
      buffered |= I8042_PushByte<PointerDeviceContext>(ctx->mouseDevice, data);
    } else {
      buffered |= I8042_PushByte<KeyboardDeviceContext>(ctx->keyboardDevice, data);
    }
  }

  // KE_EVENT_Signal is documented as usable from interrupt context; this is
  // the only thing that leaves the ISR.
  if (buffered) KE_EVENT_Signal(ctx->wake, true);
}

void I8042_KeyboardIsr(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *context) {
  I8042_DrainToRings(static_cast<I8042Context *>(context));
}

void I8042_AuxIsr(const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &, void *context) {
  I8042_DrainToRings(static_cast<I8042Context *>(context));
}

// Registers \Device\Keyboard0 and \Device\PointerPort0 as children of the
// i8042 FDO (mirroring drivers/gfx_vga/vga.cpp's named-device pattern) so
// both are visible in the Object Manager namespace and the device-tree
// dump. Each now carries its own decode state -- byte ring, decoder, parsed
// HID format -- in its device extension, and its synthetic report descriptor
// is parsed exactly once, here at attach time. A descriptor that fails to
// parse means the device cannot be decoded at all, so no device is created.
void I8042_CreateNamedDevices(const kernel::KDevicePtr<kernel::KDeviceBus> &fdo, I8042Context &ctx) {
  ctx.keyboardDevice = KE_IO_CreateDeviceWithContext<KeyboardDeviceContext>(
      fdo->driverObject, "Keyboard", kernel::device_type::Keyboard,
      CreateDeviceFlags::MultipleAllowed);
  if (ctx.keyboardDevice) {
    auto *keyboard = ctx.keyboardDevice->deviceExtension.as<KeyboardDeviceContext>();
    const bool parsed = keyboard != nullptr &&
                        kernel::hid::ParseReportDescriptor(
                            kstd::span(ps2::kKeyboardReportDescriptor,
                                       sizeof(ps2::kKeyboardReportDescriptor)),
                            keyboard->format);
    if (parsed) {
      ctx.keyboardDevice->hardwareId = "input_i8042";
      KE_IO_AttachChildDevice(fdo, ctx.keyboardDevice);
    } else {
      KE_Print("i8042: keyboard HID descriptor rejected; no keyboard device");
      ctx.keyboardDevice = nullptr;
    }
  } else {
    KE_Print("i8042: Failed to create Keyboard0 device");
  }

  ctx.mouseDevice = KE_IO_CreateDeviceWithContext<PointerDeviceContext>(
      fdo->driverObject, "PointerPort", kernel::device_type::Mouse,
      CreateDeviceFlags::MultipleAllowed);
  if (ctx.mouseDevice) {
    auto *pointer = ctx.mouseDevice->deviceExtension.as<PointerDeviceContext>();
    const bool parsed = pointer != nullptr &&
                        kernel::hid::ParseReportDescriptor(
                            kstd::span(ps2::kMouseReportDescriptor,
                                       sizeof(ps2::kMouseReportDescriptor)),
                            pointer->format);
    if (parsed) {
      ctx.mouseDevice->hardwareId = "input_i8042";
      KE_IO_AttachChildDevice(fdo, ctx.mouseDevice);
    } else {
      KE_Print("i8042: mouse HID descriptor rejected; no pointer device");
      ctx.mouseDevice = nullptr;
    }
  } else {
    KE_Print("i8042: Failed to create PointerPort0 device");
  }
}

// Mouse enable/disable and sample-rate change. Masks IRQ1/IRQ12 around the
// polled command/ack exchange so the ISRs cannot steal the ack bytes; a
// report byte already queued ahead of the ack is dropped by the retry loop.
kernel::IoStatus I8042_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                     kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred) {
  (void) output;
  transferred = 0;
  if (code != IoControlCode_MouseConfigure) return kernel::IoStatus::Unsupported;
  if (device->deviceType != kernel::device_type::Mouse) return kernel::IoStatus::Unsupported;
  if (input.size() < sizeof(IoctlMouseConfig)) return kernel::IoStatus::InvalidSize;

  IoctlMouseConfig config{};
  __builtin_memcpy(&config, input.data(), sizeof(config));
  if (config.sampleRate != 0 && (config.sampleRate < 10 || config.sampleRate > 200)) {
    return kernel::IoStatus::InvalidParameter;
  }

  HAL_INTR_SetIrqMask(i8042_regs::kDefaultIrqKeyboard, true);
  HAL_INTR_SetIrqMask(i8042_regs::kDefaultIrqMouse, true);

  // Stop reporting first so no new packets are queued during the exchange.
  bool ok = I8042_WriteAuxCommand(i8042_regs::device::DisableDataReporting);
  if (ok) {
    ok = false;
    uint8_t byte = 0;
    for (int i = 0; i < 8 && I8042_ReadData(byte); ++i) {
      if (byte == i8042_regs::device::Ack) {
        ok = true;
        break;
      }
    }
  }
  if (ok && config.sampleRate != 0) ok = I8042_SetMouseSampleRate(static_cast<uint8_t>(config.sampleRate));
  if (ok && config.enable) ok = I8042_WriteAuxCommand(i8042_regs::device::EnableDataReporting) && I8042_WaitAck();

  HAL_INTR_SetIrqMask(i8042_regs::kDefaultIrqMouse, false);
  HAL_INTR_SetIrqMask(i8042_regs::kDefaultIrqKeyboard, false);

  KE_Print("i8042: DeviceControl MouseConfigure enable={} rate={} -> {}",
           config.enable, config.sampleRate, ok ? "ok" : "failed");
  return ok ? kernel::IoStatus::Success : kernel::IoStatus::DeviceError;
}

bool I8042_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateBusWithContext<I8042Context>(driver, {});
  if (!fdo) {
    KE_Print("i8042: Failed to create FDO");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
}

void I8042_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!fdo) {
    KE_Print("i8042: No FDO attached; cannot start");
    return;
  }

  auto *ctx = fdo->deviceExtension.as<I8042Context>();
  if (!ctx) {
    KE_Print("i8042: Missing I8042Context in deviceExtension");
    return;
  }

  const auto *ioRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 0);
  if (!ioRes) {
    KE_Print("i8042: No I/O resource assigned; cannot start");
    return;
  }
  ctx->ioBase = static_cast<uint16_t>(ioRes->start);

  if (const auto *irqRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Interrupt, 0)) {
    ctx->irqKeyboard = irqRes->start;
  }

  KE_Print("i8042: I/O base 0x{x}, IRQ {} / IRQ {}", ctx->ioBase, ctx->irqKeyboard, ctx->irqMouse);

  // A missing/failing controller must not panic or hang boot -- just skip
  // creating the input devices and leave the rest of the system unaffected.
  if (!I8042_InitController()) {
    KE_Print("i8042: controller initialisation failed; no input devices created");
    return;
  }

  I8042_CreateNamedDevices(fdo, *ctx);

  // The decoding worker must exist before either ISR is connected, so the
  // very first captured byte already has something to drain it.
  ctx->wake.manualReset = false;
  ctx->wake.signalled = false;
  ctx->pumpThread = KE_SCHED_CreateThread(&ps2::I8042_PumpThread, ctx, 0);
  if (!ctx->pumpThread) {
    KE_Print("i8042: Failed to create pump thread; no input will be decoded");
    return;
  }

  // Run all polled controller/device init -- keyboard already done above,
  // mouse below -- before connecting either ISR. Once the keyboard IRQ is
  // connected, the CCB's kbd-interrupt-enable bit (set by
  // I8042_InitController) means the controller raises IRQ1 for *any* byte
  // landing in the output buffer, including 0xA9's test response and every
  // byte of the mouse reset/negotiation handshake -- the ISR would race the
  // polled reads below and steal those bytes out from under them.
  if (auto *pointer = ctx->mouseDevice ? ctx->mouseDevice->deviceExtension.as<PointerDeviceContext>() : nullptr; 
    pointer != nullptr && I8042_InitMouse()) {
    I8042_NegotiateMouseExtensions(*pointer);
  } else {
    KE_Print("i8042: no mouse detected; pointer device inactive");
  }

  // Connect both ISRs now that every polled command/response exchange is
  // done: IRQ1 for real keystrokes, IRQ12 for real mouse packets (silent
  // if no mouse was found, since I8042_InitMouse leaves the CCB's aux-IRQ
  // bit unset on any failure path).
  ctx->keyboardIsr = KE_IO_ConnectInterrupt(fdo, ctx->irqKeyboard, I8042_KeyboardIsr, ctx);
  if (!ctx->keyboardIsr) {
    KE_Print("i8042: Failed to connect keyboard ISR to IRQ {}", ctx->irqKeyboard);
  }

  ctx->mouseIsr = KE_IO_ConnectInterrupt(fdo, ctx->irqMouse, I8042_AuxIsr, ctx);
  if (!ctx->mouseIsr) {
    KE_Print("i8042: Failed to connect aux ISR to IRQ {}", ctx->irqMouse);
  }
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = I8042_AddDevice;
  driver->StartDevice = I8042_StartDevice;
  driver->DeviceControl = I8042_DeviceControl;
}
