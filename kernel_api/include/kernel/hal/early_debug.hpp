
#pragma once
#include <kernel/__core.hpp>

#include "strfmt.hpp"
#include "string_view.hpp"

UNDOS_HAL_API void early_print(const char *str);
UNDOS_HAL_API void early_print_char(char c);

template<typename... Args>
void early_print_fmt(const kstd::string_view &fmt, Args &&...args) { kstd::format_dst(early_print_char, fmt, args...); }
