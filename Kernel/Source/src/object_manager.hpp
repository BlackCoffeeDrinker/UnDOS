
#pragma once
#include <kernel/kerror.hpp>
#include <stddef.h>
#include <stdint.h>

namespace kernel { struct KObject; }

constexpr char kObPathSeperatorChar = '/';

namespace kernel::objectmanager {
bool is_initialized();
void init();
uintptr_t lock_process_directory() noexcept;
void unlock_process_directory(uintptr_t state) noexcept;
size_t process_directory_count_locked() noexcept;
bool capture_processes_locked(kernel::KObject **out, size_t capacity, size_t &count) noexcept;
}
