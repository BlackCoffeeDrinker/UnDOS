#pragma once

#include <Kernel.hpp>
#include <array.hpp>

// Standard VGA I/O ports and the classic register dump for BIOS mode 0x12
// (640x480, 16 colors, 4 bit-planes). Kept private to the gfx_vga driver.
namespace vga_regs {

// region I/O ports
constexpr uint16_t kMiscOutputWrite = 0x3C2;
constexpr uint16_t kMiscOutputRead = 0x3CC;

constexpr uint16_t kSequencerIndex = 0x3C4;
constexpr uint16_t kSequencerData = 0x3C5;

constexpr uint16_t kCrtcIndex = 0x3D4;// color CRTC (mono would be 0x3B4)
constexpr uint16_t kCrtcData = 0x3D5;

constexpr uint16_t kGraphicsControllerIndex = 0x3CE;
constexpr uint16_t kGraphicsControllerData = 0x3CF;

constexpr uint16_t kAttributeControllerIndexData = 0x3C0;// index/data share a port
constexpr uint16_t kInputStatus1 = 0x3DA;                 // reading resets the attribute flip-flop
// endregion

// region Graphics Controller register indices used for planar drawing
constexpr uint8_t kGcSetReset = 0x00;
constexpr uint8_t kGcEnableSetReset = 0x01;
constexpr uint8_t kGcReadMapSelect = 0x04;
constexpr uint8_t kGcBitMask = 0x08;
// endregion

// region Mode 0x12 register dump (640x480x16, planar)
constexpr uint8_t kMiscOutput = 0xE3;

constexpr kstd::array<uint8_t, 5> kSequencer = {
    0x03, 0x01, 0x08, 0x00, 0x06};

// CRTC index 0x11 (17) holds the write-protect bit (bit 7) for indices 0-7;
// it is explicitly cleared before programming, then this table's own value
// for index 17 (0x0C, bit 7 already clear) leaves the CRTC unlocked.
constexpr kstd::array<uint8_t, 25> kCrtc = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
    0xFF};

constexpr kstd::array<uint8_t, 9> kGraphicsController = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF};

// Palette registers 0x0-0xF map 1:1 to the explicitly initialized palette16 DAC,
// followed by mode control / overscan / color-plane-enable / pel-panning /
// color-select.
constexpr kstd::array<uint8_t, 21> kAttributeController = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x01, 0x00, 0x0F, 0x00, 0x00};
// endregion

}// namespace vga_regs
