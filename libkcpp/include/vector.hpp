
#pragma once

#include <__config.hpp>

#include <initializer_list.hpp>
#include <new.hpp>
#include <reverse_iterator.hpp>
#include <utility.hpp>

namespace kstd {

template<class T>
class vector {
public:
  using value_type = T;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

  using iterator = pointer;
  using const_iterator = const_pointer;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reverse_iterator = kstd::reverse_iterator<iterator>;
  using const_reverse_iterator = kstd::reverse_iterator<const_iterator>;

  constexpr vector() noexcept = default;

  vector(size_type count, const T &value) {
    reserve(count);
    for (size_type i = 0; i < count; ++i) {
      ::new (static_cast<void *>(__data + i)) T(value);
    }
    __size = count;
  }

  explicit vector(size_type count) {
    reserve(count);
    for (size_type i = 0; i < count; ++i) {
      ::new (static_cast<void *>(__data + i)) T();
    }
    __size = count;
  }

  vector(initializer_list<T> init) {
    reserve(init.size());
    for (const auto &value : init) {
      ::new (static_cast<void *>(__data + __size)) T(value);
      ++__size;
    }
  }

  vector(const vector &other) {
    reserve(other.__size);
    for (size_type i = 0; i < other.__size; ++i) {
      ::new (static_cast<void *>(__data + i)) T(other.__data[i]);
    }
    __size = other.__size;
  }

  vector(vector &&other) noexcept
      : __data(other.__data), __size(other.__size), __capacity(other.__capacity) {
    other.__data = nullptr;
    other.__size = 0;
    other.__capacity = 0;
  }

  ~vector() {
    __destroy_all();
    ::operator delete(__data);
  }

  vector &operator=(const vector &other) {
    if (this == &other) return *this;

    vector tmp(other);
    swap(tmp);
    return *this;
  }

  vector &operator=(vector &&other) noexcept {
    if (this == &other) return *this;

    __destroy_all();
    ::operator delete(__data);

    __data = other.__data;
    __size = other.__size;
    __capacity = other.__capacity;

    other.__data = nullptr;
    other.__size = 0;
    other.__capacity = 0;

    return *this;
  }

  vector &operator=(initializer_list<T> init) {
    clear();
    reserve(init.size());
    for (const auto &value : init) {
      ::new (static_cast<void *>(__data + __size)) T(value);
      ++__size;
    }
    return *this;
  }

  // Accessors
  [[nodiscard]] reference operator[](size_type i) { return __data[i]; }
  [[nodiscard]] const_reference operator[](size_type i) const { return __data[i]; }

  [[nodiscard]] reference at(size_type i) { return __data[i]; }
  [[nodiscard]] const_reference at(size_type i) const { return __data[i]; }

  [[nodiscard]] reference front() { return __data[0]; }
  [[nodiscard]] const_reference front() const { return __data[0]; }

  [[nodiscard]] reference back() { return __data[__size - 1]; }
  [[nodiscard]] const_reference back() const { return __data[__size - 1]; }

  [[nodiscard]] pointer data() noexcept { return __data; }
  [[nodiscard]] const_pointer data() const noexcept { return __data; }

  // Iterators
  [[nodiscard]] constexpr iterator begin() noexcept { return __data; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return __data; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return __data; }

  [[nodiscard]] constexpr iterator end() noexcept { return __data + __size; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return __data + __size; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return __data + __size; }

  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  // Capacity
  [[nodiscard]] constexpr bool empty() const noexcept { return __size == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return __size; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return __capacity; }

  void reserve(size_type new_capacity) {
    if (new_capacity <= __capacity) return;
    __grow_to(new_capacity);
  }

  void shrink_to_fit() {
    if (__size == __capacity) return;
    __grow_to(__size);
  }

  // Modifiers
  void clear() noexcept {
    __destroy_all();
    __size = 0;
  }

  void push_back(const T &value) {
    __ensure_capacity_for_one_more();
    ::new (static_cast<void *>(__data + __size)) T(value);
    ++__size;
  }

  void push_back(T &&value) {
    __ensure_capacity_for_one_more();
    ::new (static_cast<void *>(__data + __size)) T(kstd::move(value));
    ++__size;
  }

  template<class... Args>
  reference emplace_back(Args &&...args) {
    __ensure_capacity_for_one_more();
    T *slot = __data + __size;
    ::new (static_cast<void *>(slot)) T(kstd::forward<Args>(args)...);
    ++__size;
    return *slot;
  }

  void pop_back() {
    if (__size == 0) return;
    --__size;
    __data[__size].~T();
  }

  void resize(size_type count) {
    if (count < __size) {
      for (size_type i = count; i < __size; ++i) {
        __data[i].~T();
      }
    } else if (count > __size) {
      reserve(count);
      for (size_type i = __size; i < count; ++i) {
        ::new (static_cast<void *>(__data + i)) T();
      }
    }
    __size = count;
  }

  void resize(size_type count, const T &value) {
    if (count < __size) {
      for (size_type i = count; i < __size; ++i) {
        __data[i].~T();
      }
    } else if (count > __size) {
      reserve(count);
      for (size_type i = __size; i < count; ++i) {
        ::new (static_cast<void *>(__data + i)) T(value);
      }
    }
    __size = count;
  }

  void swap(vector &other) noexcept {
    T *tmp_data = __data;
    size_type tmp_size = __size;
    size_type tmp_capacity = __capacity;

    __data = other.__data;
    __size = other.__size;
    __capacity = other.__capacity;

    other.__data = tmp_data;
    other.__size = tmp_size;
    other.__capacity = tmp_capacity;
  }

private:
  T *__data = nullptr;
  size_type __size = 0;
  size_type __capacity = 0;

  void __ensure_capacity_for_one_more() {
    if (__size < __capacity) return;
    size_type new_capacity = __capacity == 0 ? 4 : __capacity * 2;
    __grow_to(new_capacity);
  }

  void __grow_to(size_type new_capacity) {
    auto *new_data = static_cast<T *>(::operator new(new_capacity * sizeof(T)));

    for (size_type i = 0; i < __size; ++i) {
      ::new (static_cast<void *>(new_data + i)) T(kstd::move(__data[i]));
      __data[i].~T();
    }

    ::operator delete(__data);
    __data = new_data;
    __capacity = new_capacity;
  }

  void __destroy_all() noexcept {
    for (size_type i = 0; i < __size; ++i) {
      __data[i].~T();
    }
  }
};

}// namespace kstd
