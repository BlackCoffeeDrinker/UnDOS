#pragma once

#include <stdint.h>

// IBM PC analog gameport (the "joystick port"), a single byte-wide register
// at 0x201. Writing any value starts an RC discharge; each of the four axis
// bits then stays set for a time proportional to its potentiometer and
// clears when that axis' one-shot expires. The four button bits are
// active-low and readable at any time. The port has no interrupt at all.
namespace gameport_regs {

constexpr uint16_t kDefaultIoBase = 0x201;

namespace status {
constexpr uint8_t Axis0 = 1 << 0;// X1 -- stick 1 X
constexpr uint8_t Axis1 = 1 << 1;// Y1 -- stick 1 Y
constexpr uint8_t Axis2 = 1 << 2;// X2 -- stick 2 X / rudder
constexpr uint8_t Axis3 = 1 << 3;// Y2 -- stick 2 Y / throttle
constexpr uint8_t AxisMask = 0x0F;

constexpr uint8_t Button0 = 1 << 4;// all four are active LOW
constexpr uint8_t Button1 = 1 << 5;
constexpr uint8_t Button2 = 1 << 6;
constexpr uint8_t Button3 = 1 << 7;
constexpr uint8_t ButtonMask = 0xF0;
}// namespace status

constexpr size_t kAxisCount = 4;
constexpr size_t kButtonCount = 4;

}// namespace gameport_regs
