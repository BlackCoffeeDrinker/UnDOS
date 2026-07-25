
#pragma once

#include <__config.hpp>
#include <__iterator/iterator_traits.hpp>

#include <initializer_list.hpp>
#include <new.hpp>
#include <reverse_iterator.hpp>
#include <utility.hpp>

namespace kstd {

// Doubly-linked list using a sentinel node so that `end()` always refers to a
// valid, dereferenceable-adjacent node: `--end()` yields `back()`, and
// `++rbegin()` (i.e. decrementing from `end()`) never touches a null pointer.
template<class T>
class list {
private:
  struct __node_base {
    __node_base *__prev = this;
    __node_base *__next = this;
  };

  struct __node : __node_base {
    T __value;

    template<class... Args>
    explicit __node(Args &&...args) : __value(kstd::forward<Args>(args)...) {}
  };

public:
  using value_type = T;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  class iterator {
  public:
    using iterator_category = bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = T *;
    using reference = T &;

    constexpr iterator() noexcept = default;
    explicit constexpr iterator(__node_base *node) noexcept : __n(node) {}

    reference operator*() const { return static_cast<__node *>(__n)->__value; }
    pointer operator->() const { return &static_cast<__node *>(__n)->__value; }

    iterator &operator++() {
      __n = __n->__next;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      __n = __n->__next;
      return tmp;
    }

    iterator &operator--() {
      __n = __n->__prev;
      return *this;
    }

    iterator operator--(int) {
      iterator tmp = *this;
      __n = __n->__prev;
      return tmp;
    }

    friend bool operator==(const iterator &lhs, const iterator &rhs) { return lhs.__n == rhs.__n; }
    friend bool operator!=(const iterator &lhs, const iterator &rhs) { return lhs.__n != rhs.__n; }

  private:
    friend class list;
    __node_base *__n = nullptr;
  };

  class const_iterator {
  public:
    using iterator_category = bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = const T *;
    using reference = const T &;

    constexpr const_iterator() noexcept = default;
    explicit constexpr const_iterator(const __node_base *node) noexcept : __n(node) {}
    constexpr const_iterator(iterator it) noexcept : __n(it.__n) {}

    reference operator*() const { return static_cast<const __node *>(__n)->__value; }
    pointer operator->() const { return &static_cast<const __node *>(__n)->__value; }

    const_iterator &operator++() {
      __n = __n->__next;
      return *this;
    }

    const_iterator operator++(int) {
      const_iterator tmp = *this;
      __n = __n->__next;
      return tmp;
    }

    const_iterator &operator--() {
      __n = __n->__prev;
      return *this;
    }

    const_iterator operator--(int) {
      const_iterator tmp = *this;
      __n = __n->__prev;
      return tmp;
    }

    friend bool operator==(const const_iterator &lhs, const const_iterator &rhs) { return lhs.__n == rhs.__n; }
    friend bool operator!=(const const_iterator &lhs, const const_iterator &rhs) { return lhs.__n != rhs.__n; }

  private:
    friend class list;
    const __node_base *__n = nullptr;
  };

  using reverse_iterator = kstd::reverse_iterator<iterator>;
  using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

  constexpr list() noexcept = default;

  list(size_type count, const T &value) {
    for (size_type i = 0; i < count; ++i) {
      push_back(value);
    }
  }

  explicit list(size_type count) {
    for (size_type i = 0; i < count; ++i) {
      emplace_back();
    }
  }

  list(initializer_list<T> init) {
    for (const auto &value : init) {
      push_back(value);
    }
  }

  list(const list &other) {
    for (const auto &value : other) {
      push_back(value);
    }
  }

  list(list &&other) noexcept {
    __steal(other);
  }

  ~list() {
    clear();
  }

  list &operator=(const list &other) {
    if (this == &other) return *this;

    list tmp(other);
    swap(tmp);
    return *this;
  }

  list &operator=(list &&other) noexcept {
    if (this == &other) return *this;

    clear();
    __steal(other);

    return *this;
  }

  list &operator=(initializer_list<T> init) {
    clear();
    for (const auto &value : init) {
      push_back(value);
    }
    return *this;
  }

  // Accessors
  [[nodiscard]] reference front() { return static_cast<__node *>(__sentinel.__next)->__value; }
  [[nodiscard]] const_reference front() const { return static_cast<const __node *>(__sentinel.__next)->__value; }

  [[nodiscard]] reference back() { return static_cast<__node *>(__sentinel.__prev)->__value; }
  [[nodiscard]] const_reference back() const { return static_cast<const __node *>(__sentinel.__prev)->__value; }

  // Iterators
  [[nodiscard]] iterator begin() noexcept { return iterator(__sentinel.__next); }
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(__sentinel.__next); }
  [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(__sentinel.__next); }

  [[nodiscard]] iterator end() noexcept { return iterator(&__sentinel); }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator(&__sentinel); }
  [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(&__sentinel); }

  [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  // Capacity
  [[nodiscard]] constexpr bool empty() const noexcept { return __size == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return __size; }

  // Modifiers
  void clear() noexcept {
    __node_base *cur = __sentinel.__next;
    while (cur != &__sentinel) {
      __node_base *next = cur->__next;
      auto *node = static_cast<__node *>(cur);
      node->~__node();
      ::operator delete(node);
      cur = next;
    }
    __sentinel.__next = &__sentinel;
    __sentinel.__prev = &__sentinel;
    __size = 0;
  }

  void push_back(const T &value) { __link_before(&__sentinel, __make_node(value)); }
  void push_back(T &&value) { __link_before(&__sentinel, __make_node(kstd::move(value))); }

  void push_front(const T &value) { __link_before(__sentinel.__next, __make_node(value)); }
  void push_front(T &&value) { __link_before(__sentinel.__next, __make_node(kstd::move(value))); }

  template<class... Args>
  reference emplace_back(Args &&...args) {
    __node *node = __make_node(kstd::forward<Args>(args)...);
    __link_before(&__sentinel, node);
    return node->__value;
  }

  template<class... Args>
  reference emplace_front(Args &&...args) {
    __node *node = __make_node(kstd::forward<Args>(args)...);
    __link_before(__sentinel.__next, node);
    return node->__value;
  }

  void pop_back() {
    if (empty()) return;
    __unlink_and_destroy(__sentinel.__prev);
  }

  void pop_front() {
    if (empty()) return;
    __unlink_and_destroy(__sentinel.__next);
  }

  iterator insert(const_iterator pos, const T &value) {
    return __insert(pos, __make_node(value));
  }

  iterator insert(const_iterator pos, T &&value) {
    return __insert(pos, __make_node(kstd::move(value)));
  }

  iterator erase(const_iterator pos) {
    auto *node = const_cast<__node_base *>(pos.__n);
    __node_base *next = node->__next;
    __unlink_and_destroy(node);
    return iterator(next);
  }

  void swap(list &other) noexcept {
    list tmp;
    tmp.__steal(*this);
    __steal(other);
    other.__steal(tmp);
  }

private:
  __node_base __sentinel{};
  size_type __size = 0;

  template<class... Args>
  __node *__make_node(Args &&...args) {
    auto *node = static_cast<__node *>(::operator new(sizeof(__node)));
    ::new (static_cast<void *>(node)) __node(kstd::forward<Args>(args)...);
    return node;
  }

  void __link_before(__node_base *pos, __node *node) {
    __node_base *prev = pos->__prev;
    node->__prev = prev;
    node->__next = pos;
    prev->__next = node;
    pos->__prev = node;
    ++__size;
  }

  void __unlink_and_destroy(__node_base *node) noexcept {
    __node_base *prev = node->__prev;
    __node_base *next = node->__next;
    prev->__next = next;
    next->__prev = prev;

    auto *typed = static_cast<__node *>(node);
    typed->~__node();
    ::operator delete(typed);
    --__size;
  }

  iterator __insert(const_iterator pos, __node *node) {
    auto *at = const_cast<__node_base *>(pos.__n);
    __link_before(at, node);
    return iterator(node);
  }

  // Rebinds this list's sentinel/size to take ownership of `other`'s
  // contents, leaving `other` empty. Used by the move constructor, move
  // assignment, and swap.
  void __steal(list &other) noexcept {
    if (other.empty()) {
      __sentinel.__next = &__sentinel;
      __sentinel.__prev = &__sentinel;
      __size = 0;
      return;
    }

    __sentinel.__next = other.__sentinel.__next;
    __sentinel.__prev = other.__sentinel.__prev;
    __sentinel.__next->__prev = &__sentinel;
    __sentinel.__prev->__next = &__sentinel;
    __size = other.__size;

    other.__sentinel.__next = &other.__sentinel;
    other.__sentinel.__prev = &other.__sentinel;
    other.__size = 0;
  }
};

}// namespace kstd
