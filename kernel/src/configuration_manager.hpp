
#pragma once

namespace kernel {

namespace configuration_manager {
void init();

// Copies every key created under the volatile "Boot" hive's "/Drivers"
// folder (see KE_DRIVER_Load()) into "/LocalMachine/Drivers", then drops the
// "Boot" hive: called once the real root filesystem is mounted and
// "LocalMachine" can actually be opened/persisted. Since "Boot" only ever
// holds fresh per-driver bootstrap keys (never a copy of previously
// persisted data), this is a plain create-or-overwrite, no merge needed.
void PromoteBootHive();
}
}// namespace kernel
