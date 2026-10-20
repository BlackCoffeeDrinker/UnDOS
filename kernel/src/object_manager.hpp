
#pragma once
#include <kernel/kerror.hpp>

constexpr char kObPathSeperatorChar = '/';

namespace kernel::objectmanager {
bool is_initialized();
kstd::error_code init();
}
