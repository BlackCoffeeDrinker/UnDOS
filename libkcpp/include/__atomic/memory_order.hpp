
#pragma once

#include <__config.hpp>
#include <__type_traits/is_same.hpp>
#include <__type_traits/underlying_type.hpp>

namespace kstd {
// Figure out what the underlying type for `memory_order` would be if it were
// declared as an unscoped enum (accounting for -fshort-enums). Use this result
// to pin the underlying type in C++20.
enum __legacy_memory_order { __mo_relaxed,
                             __mo_consume,
                             __mo_acquire,
                             __mo_release,
                             __mo_acq_rel,
                             __mo_seq_cst };

using __memory_order_underlying_t = underlying_type_t<__legacy_memory_order>;

enum class memory_order : __memory_order_underlying_t {
  relaxed = __mo_relaxed,
  consume = __mo_consume,
  acquire = __mo_acquire,
  release = __mo_release,
  acq_rel = __mo_acq_rel,
  seq_cst = __mo_seq_cst
};

static_assert(is_same<underlying_type_t<memory_order>, __memory_order_underlying_t>::value,
              "unexpected underlying type for std::memory_order");

inline constexpr auto memory_order_relaxed = memory_order::relaxed;
inline constexpr auto memory_order_consume = memory_order::consume;
inline constexpr auto memory_order_acquire = memory_order::acquire;
inline constexpr auto memory_order_release = memory_order::release;
inline constexpr auto memory_order_acq_rel = memory_order::acq_rel;
inline constexpr auto memory_order_seq_cst = memory_order::seq_cst;

}// namespace kstd
