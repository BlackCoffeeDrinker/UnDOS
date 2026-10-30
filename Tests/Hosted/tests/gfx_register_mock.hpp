#pragma once

#include <Kernel.hpp>
#include <cstdlib>

namespace gfx_register_mock {
inline uint8_t sequencer[256]{}, crtc[256]{}, graphics[256]{};
inline uint8_t sequencerIndex{}, crtcIndex{}, graphicsIndex{};
inline bool attributeData{}, attributeVideo{};
inline bool requireBlank{}, unblanked{};
inline bool extensionsLocked{};
inline uint8_t hiddenDac{}, dacReads{};
inline size_t writes{};

inline void Out8(uint16_t port, uint8_t value) noexcept {
  ++writes;
  switch (port) {
    case 0x3c4: sequencerIndex = value; break;
    case 0x3c5:
      sequencer[sequencerIndex] = value;
      if (requireBlank && sequencerIndex == 1 && !(value & 0x20))
        unblanked = true;
      break;
    case 0x3d4: crtcIndex = value; break;
    case 0x3d5: crtc[crtcIndex] = value; break;
    case 0x3ce: graphicsIndex = value; break;
    case 0x3cf: graphics[graphicsIndex] = value; break;
    case 0x3c6:
      if (dacReads == 4) hiddenDac = value;
      dacReads = 0;
      break;
    case 0x3c0:
      if (!attributeData) attributeVideo = (value & 0x20) != 0;
      attributeData = !attributeData;
      break;
    default: break;
  }
}
inline uint8_t In8(uint16_t port) noexcept {
  switch (port) {
    case 0x3c5:
      return extensionsLocked && sequencerIndex == 6 ? 0 : sequencer[sequencerIndex];
    case 0x3d5: return crtc[crtcIndex];
    case 0x3cf: return graphics[graphicsIndex];
    case 0x3da: attributeData = false; return 0;
    case 0x3c6:
      if (dacReads < 4) ++dacReads;
      return 0xff;
    default: return 0;
  }
}
inline const kernel::CmPartialResourceDescriptor *FindAssigned(
    const kernel::KDevicePtr<kernel::KDevice> &, kernel::ResourceType, size_t) noexcept {
  std::abort();
}
inline void ReleaseResources(const kernel::KDevicePtr<kernel::KDevice> &) noexcept {
  std::abort();
}
}
