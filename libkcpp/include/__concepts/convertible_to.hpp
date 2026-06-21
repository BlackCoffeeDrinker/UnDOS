
#pragma once
#include <__config.hpp>

namespace kstd {
template<class _From, class _To>
concept convertible_to = is_convertible_v<_From, _To> && requires { static_cast<_To>(kstd::declval<_From>()); };
}// namespace kstd
