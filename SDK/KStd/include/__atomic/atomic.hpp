
#pragma once

#include <__config.hpp>
#include <__atomic/memory_order.hpp>

namespace kstd {
template<typename T>
class atomic {
  T _value;

  public:
  constexpr atomic() noexcept = default;
  constexpr atomic(T value) noexcept : _value(value) {}

  atomic(const atomic &) = delete;
  atomic &operator=(const atomic &) = delete;

  T load(memory_order order = memory_order_seq_cst) const noexcept {
    return __atomic_load_n(&_value, static_cast<int>(order));
  }

  void store(T value, memory_order order = memory_order_seq_cst) noexcept {
    __atomic_store_n(&_value, value, static_cast<int>(order));
  }

  T fetch_add(T arg, memory_order order = memory_order_seq_cst) noexcept {
    return __atomic_fetch_add(&_value, arg, static_cast<int>(order));
  }

  T fetch_sub(T arg, memory_order order = memory_order_seq_cst) noexcept {
    return __atomic_fetch_sub(&_value, arg, static_cast<int>(order));
  }

  bool compare_exchange_weak(T &expected, T desired, memory_order success, memory_order failure) noexcept {
    return __atomic_compare_exchange_n(&_value, &expected, desired, true, static_cast<int>(success), static_cast<int>(failure));
  }

  bool compare_exchange_strong(T &expected, T desired, memory_order success, memory_order failure) noexcept {
    return __atomic_compare_exchange_n(&_value, &expected, desired, false, static_cast<int>(success), static_cast<int>(failure));
  }

  bool compare_exchange_weak(T &expected, T desired, memory_order order = memory_order_seq_cst) noexcept {
    return compare_exchange_weak(expected, desired, order, order);
  }

  bool compare_exchange_strong(T &expected, T desired, memory_order order = memory_order_seq_cst) noexcept {
    return compare_exchange_strong(expected, desired, order, order);
  }

  T operator++() noexcept { return fetch_add(1) + 1; }
  T operator++(int) noexcept { return fetch_add(1); }
  T operator--() noexcept { return fetch_sub(1) - 1; }
  T operator--(int) noexcept { return fetch_sub(1); }
};

inline void atomic_thread_fence(memory_order order) noexcept {
  __atomic_thread_fence(static_cast<int>(order));
}
}// namespace kstd
