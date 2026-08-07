
#include <Kernel.hpp>
#include <kernel/resource.hpp>

#include "i8042_registers.hpp"

namespace {

// Per-controller state, held in the FDO's deviceExtension (mirrors
// drivers/disk_ata/ata.cpp's BusContext). Aux enable and IRQ connection are
// added in later Step 1 tasks; for now this stacks the FDO on the PDO,
// resolves/logs the resources PnP assigned, runs controller/keyboard init
// and registers the two named input devices.
struct I8042Context {
  uint16_t ioBase = 0;// data/status/command port base (0x60), resolved from the grant

  // IRQ1 (keyboard) is normally granted through the same registry
  // reservation as the port range (see config.py / legacy_isa_enum.cpp);
  // IRQ12 (aux/mouse) has no registry-driven grant since a reservation only
  // carries a single Irq value, so it stays at its hardcoded default.
  uint32_t irqKeyboard = i8042_regs::kDefaultIrqKeyboard;
  uint32_t irqMouse = i8042_regs::kDefaultIrqMouse;

  // \Device\Keyboard0 / \Device\PointerPort0, created once controller init
  // succeeds (Task 7). Kept here so later tasks (ISR wiring, mouse init)
  // can find them without walking the FDO's child list.
  kernel::KDevicePtr<kernel::KDevice> keyboardDevice{nullptr};
  kernel::KDevicePtr<kernel::KDevice> mouseDevice{nullptr};
};

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
    early_print("i8042: WriteCommand(DisableKeyboardPort) timed out\r\n");
    return false;
  }
  if (!I8042_WriteCommand(i8042_regs::command::DisableAuxPort)) {
    early_print("i8042: WriteCommand(DisableAuxPort) timed out\r\n");
    return false;
  }

  I8042_FlushOutputBuffer();

  if (!I8042_WriteCommand(i8042_regs::command::SelfTest)) {
    early_print("i8042: WriteCommand(SelfTest) timed out\r\n");
    return false;
  }
  uint8_t selfTestResult = 0;
  if (!I8042_ReadData(selfTestResult)) {
    early_print("i8042: self-test response timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: self-test=0x{x}\r\n", selfTestResult);
  if (selfTestResult != 0x55) {
    early_print("i8042: controller self-test failed\r\n");
    return false;
  }

  if (!I8042_WriteCommand(i8042_regs::command::TestKeyboardPort)) {
    early_print("i8042: WriteCommand(TestKeyboardPort) timed out\r\n");
    return false;
  }
  uint8_t kbdTestResult = 0;
  if (!I8042_ReadData(kbdTestResult)) {
    early_print("i8042: keyboard interface test response timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: keyboard interface test=0x{x}\r\n", kbdTestResult);
  if (kbdTestResult != 0x00) {
    early_print("i8042: keyboard interface test failed\r\n");
    return false;
  }

  if (!I8042_WriteCommand(i8042_regs::command::ReadCommandByte)) {
    early_print("i8042: WriteCommand(ReadCommandByte) timed out\r\n");
    return false;
  }
  uint8_t ccb = 0;
  if (!I8042_ReadData(ccb)) {
    early_print("i8042: read CCB timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: CCB (before)=0x{x}\r\n", ccb);

  ccb |= i8042_regs::ccb::KeyboardInterruptEnable;
  ccb |= i8042_regs::ccb::KeyboardTranslation;
  ccb &= static_cast<uint8_t>(~i8042_regs::ccb::KeyboardClockDisable);

  if (!I8042_WriteCommand(i8042_regs::command::WriteCommandByte)) {
    early_print("i8042: WriteCommand(WriteCommandByte) timed out\r\n");
    return false;
  }
  if (!I8042_WriteData(ccb)) {
    early_print("i8042: write CCB timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: CCB (after)=0x{x}\r\n", ccb);

  if (!I8042_WriteCommand(i8042_regs::command::EnableKeyboardPort)) {
    early_print("i8042: WriteCommand(EnableKeyboardPort) timed out\r\n");
    return false;
  }

  if (!I8042_WriteData(i8042_regs::device::Reset)) {
    early_print("i8042: keyboard reset write timed out\r\n");
    return false;
  }
  uint8_t ack = 0;
  if (!I8042_ReadData(ack)) {
    early_print("i8042: keyboard reset ack timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: reset ack=0x{x}\r\n", ack);
  if (ack != i8042_regs::device::Ack) {
    early_print("i8042: keyboard reset not acknowledged\r\n");
    return false;
  }
  uint8_t bat = 0;
  if (!I8042_ReadData(bat)) {
    early_print("i8042: keyboard BAT timed out\r\n");
    return false;
  }
  early_print_fmt("i8042: BAT=0x{x}\r\n", bat);
  if (bat != i8042_regs::device::BatPass) {
    early_print("i8042: keyboard BAT failed\r\n");
    return false;
  }

  return true;
}

// Registers \Device\Keyboard0 and \Device\PointerPort0 as children of the
// i8042 FDO (mirroring drivers/gfx_vga/vga.cpp's named-device pattern) so
// both are visible in the Object Manager namespace and the device-tree
// dump. No device extension is needed yet -- the input manager (Task 8+)
// owns all decode state -- so these are created with 0 extension size.
void I8042_CreateNamedDevices(const kernel::KDevicePtr<kernel::KDeviceBus> &fdo, I8042Context &ctx) {
  ctx.keyboardDevice = KE_IO_CreateDevice(fdo->driverObject, 0, "Keyboard", kernel::device_type::Keyboard);
  if (ctx.keyboardDevice) {
    ctx.keyboardDevice->hardwareId = "input_i8042";
    KE_IO_AttachChildDevice(fdo, ctx.keyboardDevice);
  } else {
    early_print("i8042: Failed to create Keyboard0 device\r\n");
  }

  ctx.mouseDevice = KE_IO_CreateDevice(fdo->driverObject, 0, "PointerPort", kernel::device_type::Mouse);
  if (ctx.mouseDevice) {
    ctx.mouseDevice->hardwareId = "input_i8042";
    KE_IO_AttachChildDevice(fdo, ctx.mouseDevice);
  } else {
    early_print("i8042: Failed to create PointerPort0 device\r\n");
  }
}

bool I8042_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver, const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = KE_IO_CreateBusWithContext<I8042Context>(driver, {});
  if (!fdo) {
    early_print("i8042: Failed to create FDO\r\n");
    return false;
  }

  KE_IO_AttachFilterDevice(fdo, pdo);
  return true;
}

void I8042_StartDevice(const kernel::KDevicePtr<kernel::KDevice> &pdo) {
  const auto fdo = pdo->attachedDevice.As<kernel::KDeviceBus>();
  if (!fdo) {
    early_print("i8042: No FDO attached; cannot start\r\n");
    return;
  }

  auto *ctx = fdo->deviceExtension.as<I8042Context>();
  if (!ctx) {
    early_print("i8042: Missing I8042Context in deviceExtension\r\n");
    return;
  }

  const auto *ioRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Port, 0);
  if (!ioRes) {
    early_print("i8042: No I/O resource assigned; cannot start\r\n");
    return;
  }
  ctx->ioBase = static_cast<uint16_t>(ioRes->start);

  if (const auto *irqRes = KE_RES_FindAssigned(pdo, kernel::ResourceType::Interrupt, 0)) {
    ctx->irqKeyboard = irqRes->start;
  }

  early_print_fmt("i8042: I/O base 0x{x}, IRQ {} / IRQ {}\r\n", ctx->ioBase, ctx->irqKeyboard, ctx->irqMouse);

  // A missing/failing controller must not panic or hang boot -- just skip
  // creating the input devices and leave the rest of the system unaffected.
  if (!I8042_InitController()) {
    early_print("i8042: controller initialisation failed; no input devices created\r\n");
    return;
  }

  I8042_CreateNamedDevices(fdo, *ctx);
}

}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = I8042_AddDevice;
  driver->StartDevice = I8042_StartDevice;
}
