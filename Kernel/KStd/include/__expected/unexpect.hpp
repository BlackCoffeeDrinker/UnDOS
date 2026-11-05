
#pragma once
#include <__config.hpp>

namespace kstd {

struct unexpect_t {
    explicit unexpect_t() = default;
};

inline constexpr unexpect_t unexpect{};

} // namespace kstd
