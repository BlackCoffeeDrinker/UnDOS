#pragma once

#include <kernel/__core.hpp>

// Register/command/status-bit constants for the Intel 8042 PS/2 controller.
// Kept header-only and dependency-free (no kernel calls) so the numeric
// layout stays in one place for both i8042.cpp and any future hosted test.
namespace i8042_regs {

// I/O ports.
constexpr uint16_t kDataPort = 0x60;   // R: read a byte (kbd or aux); W: write to kbd/aux
constexpr uint16_t kStatusPort = 0x64; // R: status register
constexpr uint16_t kCommandPort = 0x64;// W: controller command register

// Status register bits (read from kStatusPort).
namespace status {
constexpr uint8_t OutputBufferFull = 0x01;// OBF: a byte is waiting at kDataPort
constexpr uint8_t InputBufferFull = 0x02; // IBF: controller hasn't consumed the last write yet
constexpr uint8_t AuxData = 0x20;         // set when the waiting OBF byte came from the aux (mouse) port
}// namespace status

// Controller commands (written to kCommandPort).
namespace command {
constexpr uint8_t ReadCommandByte = 0x20; // -> byte appears at kDataPort
constexpr uint8_t WriteCommandByte = 0x60;// next byte written to kDataPort becomes the CCB
constexpr uint8_t DisableAuxPort = 0xA7;
constexpr uint8_t EnableAuxPort = 0xA8;
constexpr uint8_t TestAuxPort = 0xA9;// -> 0x00 = pass
constexpr uint8_t SelfTest = 0xAA;   // -> 0x55 = pass
constexpr uint8_t TestKeyboardPort = 0xAB;// -> 0x00 = pass
constexpr uint8_t DisableKeyboardPort = 0xAD;
constexpr uint8_t EnableKeyboardPort = 0xAE;
constexpr uint8_t WriteToAuxPort = 0xD4;// next data byte is forwarded to the aux device
}// namespace command

// Device-level command/response bytes, written to / read from kDataPort
// (routed to the keyboard or, from Task 14 onward, to the aux device via
// command::WriteToAuxPort). Distinct from the controller-level `command`
// namespace above, which targets the 8042 chip itself via kCommandPort.
namespace device {
constexpr uint8_t Reset = 0xFF;  // -> Ack then BatPass on success
constexpr uint8_t Ack = 0xFA;
constexpr uint8_t BatPass = 0xAA;

// Aux (mouse) device commands, used from Task 14/15 onward, always sent
// through command::WriteToAuxPort so the controller forwards the next data
// byte to the aux device instead of the keyboard.
constexpr uint8_t SetDefaults = 0xF6;
constexpr uint8_t SetSampleRate = 0xF3;// followed by a data byte (samples/sec)
constexpr uint8_t GetDeviceId = 0xF2;  // -> one data byte: 0x00 plain, 0x03 wheel, 0x04 5-button
constexpr uint8_t EnableDataReporting = 0xF4;
constexpr uint8_t DisableDataReporting = 0xF5;
}// namespace device

// Controller Command Byte (CCB) bits, read/written via command::ReadCommandByte /
// command::WriteCommandByte.
namespace ccb {
constexpr uint8_t KeyboardInterruptEnable = 1 << 0;
constexpr uint8_t AuxInterruptEnable = 1 << 1;
constexpr uint8_t KeyboardClockDisable = 1 << 4;
constexpr uint8_t AuxClockDisable = 1 << 5;
constexpr uint8_t KeyboardTranslation = 1 << 6;// scancode set 2 -> set 1 translation
}// namespace ccb

// The registry's LegacyISAReservations\i8042 entry only carries a single Irq
// value (see kernel/src/legacy_isa_enum.cpp), so IRQ12 (the aux/mouse line)
// has no registry-driven grant to resolve. Both defaults are hardcoded here
// per the Step 1 plan, rather than extending the legacy enumerator for a
// second Irq value that nothing else currently needs.
constexpr uint32_t kDefaultIrqKeyboard = 1; // IRQ1
constexpr uint32_t kDefaultIrqMouse = 12;   // IRQ12

}// namespace i8042_regs
