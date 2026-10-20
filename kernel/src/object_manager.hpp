
#pragma once
#include <kernel/kerror.hpp>

constexpr char kObPathSeperatorChar = '/';

namespace kernel::objectmanager {
bool is_initialized();
void init();
}
