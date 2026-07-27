
#pragma once

#include <chrono.hpp>
#include <static_string.hpp>

#include <kernel/configuration_manager/configuration_value_type.hpp>

namespace kernel {


struct KConfigurationKeyInfo {
  kstd::chrono::system_time lastModified;
  kstd::static_string<64> name;
  bool isFolder;
  ConfigurationValueType typeIfNotFolder;
};

}
