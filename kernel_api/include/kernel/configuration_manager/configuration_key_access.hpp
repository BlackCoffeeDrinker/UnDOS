
#pragma once

#include <stdint.h>

namespace kernel {
enum class KConfigurationKeyAccess : uint16_t {
  Read = 0x01,
  Write = 0x02,
  ReadWrite = Read | Write,
  Append = 0x04,
  Truncate = 0x08,
  Create = 0x10,   // Create if not exists
  Exclusive = 0x20,// Fail if exists
  Key = 0x40,      // Opening a key, not an element
};

inline KConfigurationKeyAccess operator|(KConfigurationKeyAccess a, KConfigurationKeyAccess b) {
  return static_cast<KConfigurationKeyAccess>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline bool operator&(KConfigurationKeyAccess a, KConfigurationKeyAccess b) {
  return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}
}
