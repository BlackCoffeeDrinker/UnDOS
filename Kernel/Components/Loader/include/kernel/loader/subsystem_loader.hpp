#pragma once

#include "stdkrn.hpp"

#include <kernel/KObject.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>
#include <string_view.hpp>

namespace kernel::subsystem {
// Loads every subsystem under LocalMachine's "/Subsystems" key whose "Start"
// value is "Automatic". Call after the Automatic driver pass.
void LoadAutomaticStartSubsystems();


// Loads a subsystem image, loading its required modules first.
kstd::expected<KObjectPtr<KSubsystemObject>, kstd::error_code> Load(const kstd::string_view &path);
}// namespace kernel::subsystem
