
#pragma once

#include <kernel/kerror.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <string_view.hpp>

namespace kernel {

namespace configuration_manager {
void init();

kstd::error_code PrepareRootVolumeSwitch();
kstd::error_code PromoteBootHive(const KDevicePtr<KDevice> &selectedVolume);
}
}// namespace kernel
