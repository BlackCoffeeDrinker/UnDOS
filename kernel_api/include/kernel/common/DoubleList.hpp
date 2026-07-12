
#pragma once

#include <kernel/common/DoubleListLink.hpp>

#include <type_traits.hpp>

namespace kernel::common {

template<typename ItemType>
struct DoubleList {
  using value_type = ItemType;
  using reference = ItemType &;
  using const_reference = const ItemType &;
  using pointer = ItemType *;
  using const_pointer = const ItemType *;
  using size_type = size_t;

  class iterator {
    ItemType *node;
    friend struct DoubleList;
public:
    using value_type = ItemType;
    using reference = ItemType &;
    using pointer = ItemType *;

    iterator() : node(nullptr) {}
    explicit iterator(ItemType *n) : node(n) {}

    reference operator*() const { return *node; }
    pointer operator->() const { return node; }

    iterator &operator++() {
      node = node->next;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    iterator &operator--() {
      node = node->prev;
      return *this;
    }

    iterator operator--(int) {
      iterator tmp = *this;
      --(*this);
      return tmp;
    }

    bool operator==(const iterator &other) const { return node == other.node; }
    bool operator!=(const iterator &other) const { return node != other.node; }
  };

  class const_iterator {
    const ItemType *node;

public:
    using value_type = const ItemType;
    using reference = const ItemType &;
    using pointer = const ItemType *;

    const_iterator() : node(nullptr) {}
    explicit const_iterator(const ItemType *n) : node(n) {}
    const_iterator(const iterator &it) : node(it.node) {}

    reference operator*() const { return *node; }
    pointer operator->() const { return node; }

    const_iterator &operator++() {
      node = node->next;
      return *this;
    }

    const_iterator operator++(int) {
      const_iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    const_iterator &operator--() {
      node = node->prev;
      return *this;
    }

    const_iterator operator--(int) {
      const_iterator tmp = *this;
      --(*this);
      return tmp;
    }

    bool operator==(const const_iterator &other) const { return node == other.node; }
    bool operator!=(const const_iterator &other) const { return node != other.node; }
  };

  DoubleList() : head(nullptr), tail(nullptr), count(0) {}

  ~DoubleList() { clear(); }

  iterator begin() { return iterator(head); }
  iterator end() { return iterator(nullptr); }
  const_iterator begin() const { return const_iterator(head); }
  const_iterator end() const { return const_iterator(nullptr); }
  const_iterator cbegin() const { return const_iterator(head); }
  const_iterator cend() const { return const_iterator(nullptr); }

  [[nodiscard]] bool empty() const { return head == nullptr; }
  [[nodiscard]] size_type size() const { return count; }

  reference front() { return *head; }
  const_reference front() const { return *head; }
  reference back() { return *tail; }
  const_reference back() const { return *tail; }

  void push_back(ItemType *item) {
    item->prev = tail;
    item->next = nullptr;
    if (tail) {
      tail->next = item;
    } else {
      head = item;
    }
    tail = item;
    ++count;
  }

  void push_front(ItemType *item) {
    item->prev = nullptr;
    item->next = head;
    if (head) {
      head->prev = item;
    } else {
      tail = item;
    }
    head = item;
    ++count;
  }

  void pop_back() {
    if (!tail) return;
    ItemType *old_tail = tail;
    tail = tail->prev;
    if (tail) {
      tail->next = nullptr;
    } else {
      head = nullptr;
    }
    old_tail->prev = old_tail->next = nullptr;
    --count;
  }

  void pop_front() {
    if (!head) return;
    ItemType *old_head = head;
    head = head->next;
    if (head) {
      head->prev = nullptr;
    } else {
      tail = nullptr;
    }
    old_head->prev = old_head->next = nullptr;
    --count;
  }

  iterator insert(iterator pos, ItemType *item) {
    if (pos == end()) {
      push_back(item);
      return iterator(tail);
    }
    if (pos == begin()) {
      push_front(item);
      return iterator(head);
    }

    ItemType *next_node = pos.node;
    ItemType *prev_node = next_node->prev;

    item->prev = prev_node;
    item->next = next_node;
    prev_node->next = item;
    next_node->prev = item;
    ++count;
    return iterator(item);
  }

  iterator erase(iterator pos) {
    if (pos == end()) return end();

    ItemType *node = pos.node;
    ItemType *next_node = node->next;
    ItemType *prev_node = node->prev;

    if (prev_node) {
      prev_node->next = next_node;
    } else {
      head = next_node;
    }

    if (next_node) {
      next_node->prev = prev_node;
    } else {
      tail = prev_node;
    }

    node->prev = node->next = nullptr;
    --count;
    return iterator(next_node);
  }

  void remove(ItemType *item) {
    if (!item) return;

    if (item->prev) {
      item->prev->next = item->next;
    } else {
      head = item->next;
    }

    if (item->next) {
      item->next->prev = item->prev;
    } else {
      tail = item->prev;
    }

    item->prev = item->next = nullptr;
    --count;
  }

  void clear() {
    while (!empty()) {
      pop_front();
    }
  }

  ItemType *find(const auto &key) const {
    for (ItemType *current = head; current != nullptr; current = current->next) {
      if (current->name == key) {
        return current;
      }
    }
    return nullptr;
  }

  private:
  ItemType *head;
  ItemType *tail;
  size_type count;
};

}// namespace kernel::common
