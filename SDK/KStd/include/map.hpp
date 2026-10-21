
#pragma once

#include <__config.hpp>
#include <__functional/operations.hpp>
#include <__iterator/iterator_traits.hpp>

#include <initializer_list.hpp>
#include <new.hpp>
#include <reverse_iterator.hpp>
#include <utility.hpp>

namespace kstd {

// Ordered associative container backed by a red-black tree, keyed on
// `Key` with unique keys (mirrors std::map semantics). Uses a sentinel
// header node so that `end()` is a stable, dereferenceable-adjacent node:
// `header.__left`/`header.__right` always track the leftmost/rightmost
// elements, and `header.__parent` tracks the root.
template<class Key, class T, class Compare = kstd::less<Key>>
class map {
private:
  struct __node_base {
    __node_base *__parent = nullptr;
    __node_base *__left = this;
    __node_base *__right = this;
    bool __is_red = true;
  };

public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = pair<const Key, T>;
  using key_compare = Compare;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

private:
  struct __node : __node_base {
    value_type __value;

    template<class... Args>
    explicit __node(Args &&...args) : __value(kstd::forward<Args>(args)...) {}
  };

  static const Key &__key(const __node_base *n) { return static_cast<const __node *>(n)->__value.first; }

  static __node_base *__tree_min(__node_base *x) {
    while (x->__left != nullptr) x = x->__left;
    return x;
  }

  static __node_base *__tree_max(__node_base *x) {
    while (x->__right != nullptr) x = x->__right;
    return x;
  }

  static __node_base *__tree_increment(__node_base *x) {
    if (x->__right != nullptr) {
      x = x->__right;
      while (x->__left != nullptr) x = x->__left;
    } else {
      __node_base *y = x->__parent;
      while (x == y->__right) {
        x = y;
        y = y->__parent;
      }
      if (x->__right != y) x = y;
    }
    return x;
  }

  static __node_base *__tree_decrement(__node_base *x) {
    if (x->__is_red && x->__parent->__parent == x) {
      x = x->__right;
    } else if (x->__left != nullptr) {
      __node_base *y = x->__left;
      while (y->__right != nullptr) y = y->__right;
      x = y;
    } else {
      __node_base *y = x->__parent;
      while (x == y->__left) {
        x = y;
        y = y->__parent;
      }
      x = y;
    }
    return x;
  }

public:
  class iterator {
  public:
    using iterator_category = bidirectional_iterator_tag;
    using value_type = map::value_type;
    using difference_type = ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    constexpr iterator() noexcept = default;
    explicit constexpr iterator(__node_base *node) noexcept : __n(node) {}

    reference operator*() const { return static_cast<__node *>(__n)->__value; }
    pointer operator->() const { return &static_cast<__node *>(__n)->__value; }

    iterator &operator++() {
      __n = __tree_increment(__n);
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      __n = __tree_increment(__n);
      return tmp;
    }

    iterator &operator--() {
      __n = __tree_decrement(__n);
      return *this;
    }

    iterator operator--(int) {
      iterator tmp = *this;
      __n = __tree_decrement(__n);
      return tmp;
    }

    friend bool operator==(const iterator &lhs, const iterator &rhs) { return lhs.__n == rhs.__n; }
    friend bool operator!=(const iterator &lhs, const iterator &rhs) { return lhs.__n != rhs.__n; }

  private:
    friend class map;
    __node_base *__n = nullptr;
  };

  class const_iterator {
  public:
    using iterator_category = bidirectional_iterator_tag;
    using value_type = map::value_type;
    using difference_type = ptrdiff_t;
    using pointer = const value_type *;
    using reference = const value_type &;

    constexpr const_iterator() noexcept = default;
    explicit constexpr const_iterator(const __node_base *node) noexcept : __n(node) {}
    constexpr const_iterator(iterator it) noexcept : __n(it.__n) {}

    reference operator*() const { return static_cast<const __node *>(__n)->__value; }
    pointer operator->() const { return &static_cast<const __node *>(__n)->__value; }

    const_iterator &operator++() {
      __n = __tree_increment(const_cast<__node_base *>(__n));
      return *this;
    }

    const_iterator operator++(int) {
      const_iterator tmp = *this;
      __n = __tree_increment(const_cast<__node_base *>(__n));
      return tmp;
    }

    const_iterator &operator--() {
      __n = __tree_decrement(const_cast<__node_base *>(__n));
      return *this;
    }

    const_iterator operator--(int) {
      const_iterator tmp = *this;
      __n = __tree_decrement(const_cast<__node_base *>(__n));
      return tmp;
    }

    friend bool operator==(const const_iterator &lhs, const const_iterator &rhs) { return lhs.__n == rhs.__n; }
    friend bool operator!=(const const_iterator &lhs, const const_iterator &rhs) { return lhs.__n != rhs.__n; }

  private:
    friend class map;
    const __node_base *__n = nullptr;
  };

  using reverse_iterator = kstd::reverse_iterator<iterator>;
  using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

  constexpr map() noexcept = default;

  explicit map(const Compare &comp) : __comp(comp) {}

  map(initializer_list<value_type> init) {
    for (const auto &value : init) {
      insert(value);
    }
  }

  map(const map &other) : __comp(other.__comp) {
    for (const auto &value : other) {
      insert(value);
    }
  }

  map(map &&other) noexcept : __comp(kstd::move(other.__comp)) {
    __steal(other);
  }

  ~map() {
    clear();
  }

  map &operator=(const map &other) {
    if (this == &other) return *this;

    map tmp(other);
    swap(tmp);
    return *this;
  }

  map &operator=(map &&other) noexcept {
    if (this == &other) return *this;

    clear();
    __comp = kstd::move(other.__comp);
    __steal(other);

    return *this;
  }

  map &operator=(initializer_list<value_type> init) {
    clear();
    for (const auto &value : init) {
      insert(value);
    }
    return *this;
  }

  // Iterators
  [[nodiscard]] iterator begin() noexcept { return iterator(__header.__left); }
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(__header.__left); }
  [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(__header.__left); }

  [[nodiscard]] iterator end() noexcept { return iterator(&__header); }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator(&__header); }
  [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(&__header); }

  [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  // Capacity
  [[nodiscard]] constexpr bool empty() const noexcept { return __size == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return __size; }

  // Lookup
  [[nodiscard]] iterator find(const Key &key) {
    __node_base *n = __find_node(key);
    return n != nullptr ? iterator(n) : end();
  }

  [[nodiscard]] const_iterator find(const Key &key) const {
    const __node_base *n = __find_node(key);
    return n != nullptr ? const_iterator(n) : end();
  }

  [[nodiscard]] bool contains(const Key &key) const { return __find_node(key) != nullptr; }

  [[nodiscard]] size_type count(const Key &key) const { return __find_node(key) != nullptr ? 1 : 0; }

  [[nodiscard]] iterator lower_bound(const Key &key) { return iterator(__lower_bound_node(key)); }
  [[nodiscard]] const_iterator lower_bound(const Key &key) const { return const_iterator(__lower_bound_node(key)); }

  [[nodiscard]] iterator upper_bound(const Key &key) { return iterator(__upper_bound_node(key)); }
  [[nodiscard]] const_iterator upper_bound(const Key &key) const { return const_iterator(__upper_bound_node(key)); }

  T &operator[](const Key &key) {
    __insert_pos pos = __find_insert_unique_pos(key);
    if (pos.existing != nullptr) return static_cast<__node *>(pos.existing)->__value.second;

    __node *node = __make_node(key, T());
    __insert_node(pos.parent, pos.insert_left, node);
    return node->__value.second;
  }

  T &operator[](Key &&key) {
    __insert_pos pos = __find_insert_unique_pos(key);
    if (pos.existing != nullptr) return static_cast<__node *>(pos.existing)->__value.second;

    __node *node = __make_node(kstd::move(key), T());
    __insert_node(pos.parent, pos.insert_left, node);
    return node->__value.second;
  }

  [[nodiscard]] T &at(const Key &key) { return static_cast<__node *>(__find_node(key))->__value.second; }
  [[nodiscard]] const T &at(const Key &key) const { return static_cast<const __node *>(__find_node(key))->__value.second; }

  // Modifiers
  void clear() noexcept {
    __destroy_subtree(__header.__parent);
    __header.__parent = nullptr;
    __header.__left = &__header;
    __header.__right = &__header;
    __size = 0;
  }

  pair<iterator, bool> insert(const value_type &value) {
    __insert_pos pos = __find_insert_unique_pos(value.first);
    if (pos.existing != nullptr) return {iterator(pos.existing), false};

    __node *node = __make_node(value);
    __insert_node(pos.parent, pos.insert_left, node);
    return {iterator(node), true};
  }

  pair<iterator, bool> insert(value_type &&value) {
    __insert_pos pos = __find_insert_unique_pos(value.first);
    if (pos.existing != nullptr) return {iterator(pos.existing), false};

    __node *node = __make_node(kstd::move(value));
    __insert_node(pos.parent, pos.insert_left, node);
    return {iterator(node), true};
  }

  template<class... Args>
  pair<iterator, bool> emplace(Args &&...args) {
    __node *node = __make_node(kstd::forward<Args>(args)...);
    __insert_pos pos = __find_insert_unique_pos(node->__value.first);
    if (pos.existing != nullptr) {
      node->~__node();
      ::operator delete(node);
      return {iterator(pos.existing), false};
    }

    __insert_node(pos.parent, pos.insert_left, node);
    return {iterator(node), true};
  }

  iterator erase(const_iterator pos) {
    auto *node = const_cast<__node_base *>(pos.__n);
    __node_base *next = __tree_increment(node);
    __node_base *to_delete = __rebalance_for_erase(node);
    auto *typed = static_cast<__node *>(to_delete);
    typed->~__node();
    ::operator delete(typed);
    --__size;
    return iterator(next);
  }

  size_type erase(const Key &key) {
    __node_base *node = __find_node(key);
    if (node == nullptr) return 0;
    erase(const_iterator(node));
    return 1;
  }

  void swap(map &other) noexcept {
    map tmp;
    tmp.__steal(*this);
    __steal(other);
    other.__steal(tmp);

    using kstd::swap;
    swap(__comp, other.__comp);
  }

private:
  struct __insert_pos {
    __node_base *parent;
    bool insert_left;
    __node_base *existing;
  };

  __node_base __header{};
  size_type __size = 0;
  key_compare __comp{};

  __node_base *__root() { return __header.__parent; }
  const __node_base *__root() const { return __header.__parent; }

  __node_base *__find_node(const Key &key) {
    __node_base *x = __root();
    while (x != nullptr) {
      if (__comp(key, __key(x))) x = x->__left;
      else if (__comp(__key(x), key))
        x = x->__right;
      else
        return x;
    }
    return nullptr;
  }

  const __node_base *__find_node(const Key &key) const {
    const __node_base *x = __root();
    while (x != nullptr) {
      if (__comp(key, __key(x))) x = x->__left;
      else if (__comp(__key(x), key))
        x = x->__right;
      else
        return x;
    }
    return nullptr;
  }

  __node_base *__lower_bound_node(const Key &key) const {
    __node_base *x = const_cast<__node_base *>(__root());
    __node_base *y = const_cast<__node_base *>(static_cast<const __node_base *>(&__header));
    while (x != nullptr) {
      if (!__comp(__key(x), key)) {
        y = x;
        x = x->__left;
      } else {
        x = x->__right;
      }
    }
    return y;
  }

  __node_base *__upper_bound_node(const Key &key) const {
    __node_base *x = const_cast<__node_base *>(__root());
    __node_base *y = const_cast<__node_base *>(static_cast<const __node_base *>(&__header));
    while (x != nullptr) {
      if (__comp(key, __key(x))) {
        y = x;
        x = x->__left;
      } else {
        x = x->__right;
      }
    }
    return y;
  }

  __insert_pos __find_insert_unique_pos(const Key &key) {
    __node_base *x = __root();
    __node_base *y = &__header;
    bool comp = true;
    while (x != nullptr) {
      y = x;
      comp = __comp(key, __key(x));
      x = comp ? x->__left : x->__right;
    }

    __node_base *j = y;
    if (comp) {
      if (j == __header.__left) {
        return {.parent = y, .insert_left = true, .existing = nullptr};
      }
      j = __tree_decrement(j);
    }

    if (__comp(__key(j), key)) {
      return {.parent = y, .insert_left = comp, .existing = nullptr};
    }

    return {.parent = nullptr, .insert_left = false, .existing = j};
  }

  template<class... Args>
  __node *__make_node(Args &&...args) {
    auto *node = static_cast<__node *>(::operator new(sizeof(__node)));
    ::new (static_cast<void *>(node)) __node(kstd::forward<Args>(args)...);
    return node;
  }

  void __insert_node(__node_base *parent, bool insert_left, __node *node) {
    node->__parent = parent;
    node->__left = nullptr;
    node->__right = nullptr;
    node->__is_red = true;

    if (parent == &__header) {
      __header.__parent = node;
      __header.__left = node;
      __header.__right = node;
    } else if (insert_left) {
      parent->__left = node;
      if (parent == __header.__left) __header.__left = node;
    } else {
      parent->__right = node;
      if (parent == __header.__right) __header.__right = node;
    }

    __rebalance_after_insert(node);
    ++__size;
  }

  void __rotate_left(__node_base *x) {
    __node_base *y = x->__right;
    x->__right = y->__left;
    if (y->__left != nullptr) y->__left->__parent = x;
    y->__parent = x->__parent;
    if (x == __root()) __header.__parent = y;
    else if (x == x->__parent->__left)
      x->__parent->__left = y;
    else
      x->__parent->__right = y;
    y->__left = x;
    x->__parent = y;
  }

  void __rotate_right(__node_base *x) {
    __node_base *y = x->__left;
    x->__left = y->__right;
    if (y->__right != nullptr) y->__right->__parent = x;
    y->__parent = x->__parent;
    if (x == __root()) __header.__parent = y;
    else if (x == x->__parent->__right)
      x->__parent->__right = y;
    else
      x->__parent->__left = y;
    y->__right = x;
    x->__parent = y;
  }

  void __rebalance_after_insert(__node_base *x) {
    while (x != __root() && x->__parent->__is_red) {
      __node_base *parent = x->__parent;
      __node_base *grandparent = parent->__parent;
      if (parent == grandparent->__left) {
        __node_base *uncle = grandparent->__right;
        if (uncle != nullptr && uncle->__is_red) {
          parent->__is_red = false;
          uncle->__is_red = false;
          grandparent->__is_red = true;
          x = grandparent;
        } else {
          if (x == parent->__right) {
            x = parent;
            __rotate_left(x);
            parent = x->__parent;
            grandparent = parent->__parent;
          }
          parent->__is_red = false;
          grandparent->__is_red = true;
          __rotate_right(grandparent);
        }
      } else {
        __node_base *uncle = grandparent->__left;
        if (uncle != nullptr && uncle->__is_red) {
          parent->__is_red = false;
          uncle->__is_red = false;
          grandparent->__is_red = true;
          x = grandparent;
        } else {
          if (x == parent->__left) {
            x = parent;
            __rotate_right(x);
            parent = x->__parent;
            grandparent = parent->__parent;
          }
          parent->__is_red = false;
          grandparent->__is_red = true;
          __rotate_left(grandparent);
        }
      }
    }
    __root()->__is_red = false;
  }

  __node_base *__rebalance_for_erase(__node_base *z) {
    __node_base *y = z;
    __node_base *x = nullptr;
    __node_base *x_parent = nullptr;

    if (y->__left == nullptr) {
      x = y->__right;
    } else if (y->__right == nullptr) {
      x = y->__left;
    } else {
      y = y->__right;
      while (y->__left != nullptr) y = y->__left;
      x = y->__right;
    }

    if (y != z) {
      z->__left->__parent = y;
      y->__left = z->__left;
      if (y != z->__right) {
        x_parent = y->__parent;
        if (x != nullptr) x->__parent = y->__parent;
        y->__parent->__left = x;
        y->__right = z->__right;
        z->__right->__parent = y;
      } else {
        x_parent = y;
      }

      if (__root() == z) __header.__parent = y;
      else if (z->__parent->__left == z)
        z->__parent->__left = y;
      else
        z->__parent->__right = y;

      y->__parent = z->__parent;
      {
        bool tmp = y->__is_red;
        y->__is_red = z->__is_red;
        z->__is_red = tmp;
      }
      y = z;
    } else {
      x_parent = y->__parent;
      if (x != nullptr) x->__parent = y->__parent;

      if (__root() == z) __header.__parent = x;
      else if (z->__parent->__left == z)
        z->__parent->__left = x;
      else
        z->__parent->__right = x;

      if (__header.__left == z) {
        __header.__left = (z->__right == nullptr) ? z->__parent : __tree_min(x);
      }
      if (__header.__right == z) {
        __header.__right = (z->__left == nullptr) ? z->__parent : __tree_max(x);
      }
    }

    if (!y->__is_red) {
      while (x != __root() && (x == nullptr || !x->__is_red)) {
        if (x == x_parent->__left) {
          __node_base *w = x_parent->__right;
          if (w->__is_red) {
            w->__is_red = false;
            x_parent->__is_red = true;
            __rotate_left(x_parent);
            w = x_parent->__right;
          }
          if ((w->__left == nullptr || !w->__left->__is_red) && (w->__right == nullptr || !w->__right->__is_red)) {
            w->__is_red = true;
            x = x_parent;
            x_parent = x_parent->__parent;
          } else {
            if (w->__right == nullptr || !w->__right->__is_red) {
              if (w->__left != nullptr) w->__left->__is_red = false;
              w->__is_red = true;
              __rotate_right(w);
              w = x_parent->__right;
            }
            w->__is_red = x_parent->__is_red;
            x_parent->__is_red = false;
            if (w->__right != nullptr) w->__right->__is_red = false;
            __rotate_left(x_parent);
            break;
          }
        } else {
          __node_base *w = x_parent->__left;
          if (w->__is_red) {
            w->__is_red = false;
            x_parent->__is_red = true;
            __rotate_right(x_parent);
            w = x_parent->__left;
          }
          if ((w->__right == nullptr || !w->__right->__is_red) && (w->__left == nullptr || !w->__left->__is_red)) {
            w->__is_red = true;
            x = x_parent;
            x_parent = x_parent->__parent;
          } else {
            if (w->__left == nullptr || !w->__left->__is_red) {
              if (w->__right != nullptr) w->__right->__is_red = false;
              w->__is_red = true;
              __rotate_left(w);
              w = x_parent->__left;
            }
            w->__is_red = x_parent->__is_red;
            x_parent->__is_red = false;
            if (w->__left != nullptr) w->__left->__is_red = false;
            __rotate_right(x_parent);
            break;
          }
        }
      }
      if (x != nullptr) x->__is_red = false;
    }
    return y;
  }

  void __destroy_subtree(__node_base *n) noexcept {
    if (n == nullptr) return;
    __destroy_subtree(n->__left);
    __destroy_subtree(n->__right);
    auto *typed = static_cast<__node *>(n);
    typed->~__node();
    ::operator delete(typed);
  }

  // Rebinds this map's header/size to take ownership of `other`'s
  // contents, leaving `other` empty. Used by the move constructor, move
  // assignment, and swap.
  void __steal(map &other) noexcept {
    if (other.empty()) {
      __header.__parent = nullptr;
      __header.__left = &__header;
      __header.__right = &__header;
      __size = 0;
      return;
    }

    __header.__parent = other.__header.__parent;
    __header.__left = other.__header.__left;
    __header.__right = other.__header.__right;
    __header.__parent->__parent = &__header;
    __size = other.__size;

    other.__header.__parent = nullptr;
    other.__header.__left = &other.__header;
    other.__header.__right = &other.__header;
    other.__size = 0;
  }
};

}// namespace kstd
