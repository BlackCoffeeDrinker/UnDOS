
#pragma once

#include <algorithm.hpp>
#include <stddef.hpp>
#include <type_traits.hpp>
#include <utility.hpp>

namespace kernel {
template<typename... Types>
struct ContiguousContainer {
  static constexpr kstd::size_t count = sizeof...(Types);

  // Compute offsets for each type at compile time
  static consteval auto compute_offsets() {
    kstd::array<kstd::size_t, count> offs{};
    kstd::size_t offset = 0;

    [&]<kstd::size_t... I>(kstd::index_sequence<I...>) {
      ((offs[I] = (offset = (offset + alignof(kstd::tuple_element_t<I, kstd::tuple<Types...>>) - 1) & ~(alignof(kstd::tuple_element_t<I, kstd::tuple<Types...>>) - 1)),
        offset += sizeof(kstd::tuple_element_t<I, kstd::tuple<Types...>>)),
       ...);
    }(kstd::make_index_sequence<count>{});

    return offs;
  }

  static constexpr auto offsets = compute_offsets();

  static constexpr kstd::size_t total_size = [] {
    kstd::size_t s = 0;

    [&]<kstd::size_t... I>(kstd::index_sequence<I...>) {
      ((s = kstd::max(s, offsets[I] + sizeof(kstd::tuple_element_t<I, kstd::tuple<Types...>>))), ...);
    }(kstd::make_index_sequence<count>{});

    return s;
  }();


  alignas(kstd::max({alignof(Types)...})) kstd::byte storage[total_size];

  // Construct all objects in-place
  constexpr ContiguousContainer(Types &&...args) {
    construct_all(kstd::forward<Types>(args)...);
  }

  // Destroy all objects
  ~ContiguousContainer() {
    destroy_all();
  }

  // Accessor by type index
  template<kstd::size_t I>
  constexpr auto &get() {
    using T = kstd::tuple_element_t<I, kstd::tuple<Types...>>;
    return *kstd::launder(reinterpret_cast<T *>(storage + offsets[I]));
  }

  template<kstd::size_t I>
  constexpr const auto &get() const {
    using T = kstd::tuple_element_t<I, kstd::tuple<Types...>>;
    return *kstd::launder(reinterpret_cast<const T *>(storage + offsets[I]));
  }

  private:
  template<typename... Args>
  constexpr void construct_all(Args &&...args) {
    [&]<kstd::size_t... I>(kstd::index_sequence<I...>) {
      ((new (storage + offsets[I]) kstd::tuple_element_t<I, kstd::tuple<Types...>>(
           kstd::forward<Args>(args))),
       ...);
    }(kstd::make_index_sequence<count>{});
  }

  template<kstd::size_t I>
  constexpr void destroy_one() {
    using T = kstd::tuple_element_t<I, kstd::tuple<Types...>>;
    get<I>().~T();
  }

  constexpr void destroy_all() {
    [&]<kstd::size_t... I>(kstd::index_sequence<I...>) {
      (destroy_one<I>(), ...);
    }(kstd::make_index_sequence<count>{});
  }
};

}// namespace kernel
