
#pragma once

#include <chrono.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

#include <kernel/configuration_manager/configuration_value_type.hpp>

namespace kernel {


struct KConfigurationKeyInfo {
  kstd::chrono::system_time lastModified;
  kstd::static_string<64> name;
  bool isFolder;
  ConfigurationValueType typeIfNotFolder;
};

// Successful enumeration with available=false is ordinary exhaustion, not an error.
// Names borrow the enumerated key handle's scratch storage until its next enumeration.
struct KConfigurationKeyEntry {
  bool available = false;
  kstd::string_view name;
};

// One entry produced by KE_CM_EnumValue(). `name` points into storage owned by the enumerated key handle
// and is only valid until the next enumeration on that handle.
struct KConfigurationValueEntry {
  bool available = false;
  kstd::string_view name;
  ConfigurationValueType type = ConfigurationValueType::Invalid;
};

}
