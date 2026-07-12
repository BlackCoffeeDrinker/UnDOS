
#pragma once

#include <__config.hpp>

namespace kstd {

// This tag allows defining non-standard exposition-only constructors while
// preventing users from being able to use them, since this reserved-name tag
// needs to be used.
struct __private_constructor_tag {};

}// namespace kstd
