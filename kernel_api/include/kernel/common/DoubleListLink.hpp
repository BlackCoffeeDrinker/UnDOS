
#pragma once
#include <type_traits.hpp>

namespace kernel::common {
/**
 * @brief Doubly‑linked list hook used to embed linkage pointers in user‑defined types.
 *
 * This templated link structure provides `next` and `prev` pointers so that any
 * type `T` inheriting from `Link<T>` can participate in an intrusive doubly‑linked list.
 *
 * The static assertion enforces that `T` must derive from `Link<T>`, ensuring
 * correct usage and preventing accidental misuse of unrelated types.
 *
 * @tparam T The type that embeds this link and must inherit from `Link<T>`.
 */
template<typename T>
struct Link {
  using type = T;

  T *next = nullptr;///< Pointer to the next element in the intrusive list.
  T *prev = nullptr;///< Pointer to the previous element in the intrusive list.

  constexpr Link() noexcept = default;
};

}// namespace kernel::common
