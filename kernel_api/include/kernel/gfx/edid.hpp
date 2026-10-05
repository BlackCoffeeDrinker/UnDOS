
#pragma once

#include <array.hpp>
#include <span.hpp>
#include <static_string.hpp>
#include <vector.hpp>

#include <kernel/gfx/structs.hpp>

namespace kernel {
class Edid {
  public:
  static constexpr size_t BlockSize = 128;

  private:
  void ParseMetadata() {
    // Decode compressed 3-character PnP Manufacturer ID (Bytes 0x08 - 0x09)
    const auto pnpId = static_cast<uint16_t>((static_cast<uint32_t>(_raw[0x08]) << 8) | _raw[0x09]);
    const char idStr[4] = {
        static_cast<char>(((pnpId >> 10) & 0x1F) + 'A' - 1),
        static_cast<char>(((pnpId >> 5) & 0x1F) + 'A' - 1),
        static_cast<char>((pnpId & 0x1F) + 'A' - 1),
        '\0'};
    _manufacturerId = idStr;

    // Product Code (Bytes 0x0A - 0x0B, Little Endian)
    _productCode = static_cast<uint16_t>(_raw[0x0A] | (static_cast<uint32_t>(_raw[0x0B]) << 8));

    // Physical Dimensions in millimeters (Bytes 0x15, 0x16)
    _widthMm = static_cast<uint16_t>(_raw[0x15]) * 10;
    _heightMm = static_cast<uint16_t>(_raw[0x16]) * 10;

    // Extract Monitor Name String descriptor (descriptor code 0xFC)
    for (size_t offset = 0x36; offset <= 0x6C; offset += 18) {
      auto dtd = kstd::span<const uint8_t, 18>(_raw.data() + offset, 18);
      if (dtd[0] == 0x00 && dtd[1] == 0x00 && dtd[3] == 0xFC) {// Name Descriptor
        char nameBuf[14]{};
        for (size_t i = 0; i < 13; ++i) {
          if (dtd[5 + i] == 0x0A || dtd[5 + i] == 0x0D) break;// Trailing newline
          nameBuf[i] = static_cast<char>(dtd[5 + i]);
        }
        _monitorName = nameBuf;
        break;
      }
    }
  }

  static bool ParseDetailedTimingDescriptor(kstd::span<const uint8_t> dtd, KDisplayModeInfo &outMode) {
    if (dtd.size() < 18) return false;

    const auto pixelClock10kHz = static_cast<uint16_t>(dtd[0] | (static_cast<uint32_t>(dtd[1]) << 8));
    if (pixelClock10kHz == 0) {
      return false;// Not a DTD block (can be a display descriptor)
    }

    uint32_t hactive = dtd[2] | ((static_cast<uint32_t>(dtd[4]) & 0xF0) << 4);
    uint32_t vactive = dtd[5] | ((static_cast<uint32_t>(dtd[7]) & 0xF0) << 4);
    uint32_t hblank = dtd[3] | ((static_cast<uint32_t>(dtd[4]) & 0x0F) << 8);
    uint32_t vblank = dtd[6] | ((static_cast<uint32_t>(dtd[7]) & 0x0F) << 8);

    uint32_t htotal = hactive + hblank;
    uint32_t vtotal = vactive + vblank;

    outMode.width = hactive;
    outMode.height = vactive;

    if (htotal > 0 && vtotal > 0) {
      const uint64_t clockHz = static_cast<uint64_t>(pixelClock10kHz) * 10000;
      outMode.refreshRate = static_cast<uint32_t>(clockHz / (htotal * vtotal));
    }

    if ((dtd[17] & 0x80) != 0) {
      outMode.attributeFlags |= DisplayModeFlags::Interlaced;
    }

    return true;
  }

  void ParseStandardTimings(kstd::vector<KDisplayModeInfo> &outModes) const {
    for (size_t i = 0x26; i <= 0x35; i += 2) {
      uint8_t byte1 = _raw[i];
      uint8_t byte2 = _raw[i + 1];

      if (byte1 == 0x01 && byte2 == 0x01) continue;// Unused slot

      const uint32_t width = (static_cast<uint32_t>(byte1) + 31) * 8;
      const uint32_t refresh = (byte2 & 0x3F) + 60;
      uint32_t height = 0;

      switch ((byte2 >> 6) & 0x03) {
        case 0: height = (width * 10) / 16; break;// 16:10
        case 1: height = (width * 3) / 4; break;  // 4:3
        case 2: height = (width * 4) / 5; break;  // 5:4
        case 3: height = (width * 9) / 16; break; // 16:9
      }

      KDisplayModeInfo mode{};
      mode.width = width;
      mode.height = height;
      mode.refreshRate = refresh;
      mode.xMillimeter = _widthMm;
      mode.yMillimeter = _heightMm;
      outModes.push_back(mode);
    }
  }

  kstd::array<uint8_t, MaxEdidSize> _raw{};
  uint16_t _size{0};
  bool _valid{false};

  kstd::static_string<8> _manufacturerId{};
  kstd::static_string<64> _monitorName{};
  uint16_t _productCode{0};
  uint16_t _widthMm{0};
  uint16_t _heightMm{0};

  public:
  Edid() = default;

  // Construct or assign from a contiguous hardware byte span
  explicit Edid(kstd::span<const uint8_t> data) {
    Assign(data);
  }

  // Assigns raw EDID data and validates headers/checksums
  bool Assign(kstd::span<const uint8_t> data) {
    Clear();

    if (data.empty() || data.size() < BlockSize) {
      return false;
    }

    // EDID blocks are always multiples of 128 bytes
    _size = static_cast<uint16_t>(kstd::min(data.size(), MaxEdidSize) & ~(BlockSize - 1));
    if (_size == 0) {
      return false;
    }

    // Validate 8-byte EDID header (00 FF FF FF FF FF FF 00)
    if (!ValidateHeader(data)) {
      Clear();
      return false;
    }

    // Validate 128-byte block checksums
    for (size_t block = 0; block < (_size / BlockSize); ++block) {
      if (!VerifyBlockChecksum(data.subspan(block * BlockSize, BlockSize))) {
        Clear();
        return false;
      }
    }

    // Copy raw payload
    for (size_t i = 0; i < _size; ++i) {
      _raw[i] = data[i];
    }

    _valid = true;
    ParseMetadata();
    return true;
  }

  void Clear() {
    for (auto &c: _raw) c = 0;
    _size = 0;
    _valid = false;
    _manufacturerId.clear();
    _monitorName.clear();
    _productCode = 0;
    _widthMm = 0;
    _heightMm = 0;
  }

  // Status Queries
  [[nodiscard]] constexpr bool IsValid() const { return _valid; }
  [[nodiscard]] constexpr size_t Size() const { return _size; }

  // Data Views
  [[nodiscard]] constexpr kstd::span<const uint8_t> Data() const { return {_raw.data(), _size}; }
  [[nodiscard]] constexpr const kstd::array<uint8_t, MaxEdidSize> &Raw() const { return _raw; }

  // Extracted Display Attributes
  [[nodiscard]] kstd::string_view ManufacturerId() const { return _manufacturerId; }
  [[nodiscard]] kstd::string_view MonitorName() const { return _monitorName; }
  [[nodiscard]] uint16_t ProductCode() const { return _productCode; }
  [[nodiscard]] uint16_t PhysicalWidthMm() const { return _widthMm; }
  [[nodiscard]] uint16_t PhysicalHeightMm() const { return _heightMm; }

  // Mode Extraction
  size_t ExtractSupportedModes(kstd::vector<KDisplayModeInfo> &outModes) const {
    if (!_valid) { return 0; }

    const size_t initialCount = outModes.size();

    // 1. Parse 18-byte Detailed Timing Descriptors (DTDs) from Base Block (offset 0x36)
    for (size_t offset = 0x36; offset <= 0x6C; offset += 18) {
      KDisplayModeInfo dtdMode{};
      if (ParseDetailedTimingDescriptor(kstd::span<const uint8_t, 18>(_raw.data() + offset, 18), dtdMode)) {
        dtdMode.xMillimeter = _widthMm;
        dtdMode.yMillimeter = _heightMm;
        outModes.push_back(dtdMode);
      }
    }

    // 2. Parse Standard Timings (bytes 0x26 - 0x35)
    ParseStandardTimings(outModes);

    return outModes.size() - initialCount;
  }

  // Static Helpers
  static bool ValidateHeader(kstd::span<const uint8_t> data) {
    if (data.size() < 8) return false;
    static constexpr uint8_t EdidHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    for (size_t i = 0; i < 8; ++i) {
      if (data[i] != EdidHeader[i]) return false;
    }
    return true;
  }

  static bool VerifyBlockChecksum(kstd::span<const uint8_t> block) {
    if (block.size() < BlockSize) return false;
    uint8_t sum = 0;
    for (size_t i = 0; i < BlockSize; ++i) {
      sum += block[i];
    }
    return sum == 0;// Valid block sums to 0 modulo 256
  }
};

}// namespace kernel
