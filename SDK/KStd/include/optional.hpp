
#pragma once
#include <__config.hpp>

#include <stddef.hpp>

namespace kstd {
namespace detail {

}


struct nullopt_t {
  // Do not user-declare default constructor at all for
  // optional_value = {} syntax to work.
  // nullopt_t() = delete;

  // Used for constructing nullopt.
  enum class _Construct { _Token };

  // Must be constexpr for nullopt_t to be literal.
  explicit constexpr nullopt_t(_Construct) noexcept {}
};

inline constexpr nullopt_t nullopt{nullopt_t::_Construct::_Token};

template<typename T>
struct optional {
  using value_type = T;

  constexpr optional() noexcept {}
  constexpr optional(nullopt_t) noexcept {}

  private:
  bool has_value = false;
};

}// namespace kstd
