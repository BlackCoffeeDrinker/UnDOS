#pragma once

#include <atomic.hpp>
#include <new.hpp>
#include <stddef.hpp>
#include <utility.hpp>

namespace kernel::font {

// Width conversion that is a real narrowing on 64-bit hosts and a no-op on i386;
// the template keeps -Wuseless-cast quiet in the kernel build.
template <typename T> constexpr uint32_t to_u32(T value) { return static_cast<uint32_t>(value); }

// Base for objects shared between the registry, realizations and
// drawing code. A new object holds one reference, which make_ref() adopts.
class RefCounted {
public:
  RefCounted() = default;
  RefCounted(const RefCounted &) = delete;
  RefCounted &operator=(const RefCounted &) = delete;
  void retain() const noexcept { count_.fetch_add(1, kstd::memory_order_relaxed); }
  // True when the caller dropped the last reference and must destroy the object.
  bool drop() const noexcept {
    if (count_.fetch_sub(1, kstd::memory_order_release) != 1) return false;
    kstd::atomic_thread_fence(kstd::memory_order_acquire);
    return true;
  }
  uint32_t references() const noexcept { return count_.load(kstd::memory_order_relaxed); }

protected:
  ~RefCounted() = default;

private:
  mutable kstd::atomic<uint32_t> count_{1};
};

template <typename T>
class Ref {
public:
  Ref() = default;
  Ref(const Ref &other) : ptr_(other.ptr_) { if (ptr_) ptr_->retain(); }
  Ref(Ref &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
  Ref &operator=(Ref other) noexcept {
    T *old = ptr_;
    ptr_ = other.ptr_;
    other.ptr_ = old;
    return *this;
  }
  ~Ref() { reset(); }

  // Adopts an existing reference.
  static Ref adopt(T *ptr) noexcept {
    Ref ref;
    ref.ptr_ = ptr;
    return ref;
  }

  void reset() noexcept {
    if (ptr_ && ptr_->drop()) destroy(ptr_);
    ptr_ = nullptr;
  }
  T *get() const noexcept { return ptr_; }
  T *operator->() const noexcept { return ptr_; }
  T &operator*() const noexcept { return *ptr_; }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
  static void destroy(T *ptr) noexcept {
    ptr->~T();
    ::operator delete(ptr);
  }
  T *ptr_{nullptr};
};

// Returns an empty Ref when memory is exhausted.
template <typename T, typename... Args>
Ref<T> make_ref(Args &&...args) {
  void *memory = operator new(sizeof(T));
  if (!memory) return {};
  return Ref<T>::adopt(new (memory) T(static_cast<Args &&>(args)...));
}

} // namespace kernel::font
