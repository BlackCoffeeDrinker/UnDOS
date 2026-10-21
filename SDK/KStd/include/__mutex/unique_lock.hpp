
#pragma once

#include <__config.hpp>
#include <__utility/exchange.hpp>
#include <__utility/swap.hpp>

namespace kstd {

struct defer_lock_t {
  explicit defer_lock_t() = default;
};

struct try_to_lock_t {
  explicit try_to_lock_t() = default;
};

struct adopt_lock_t {
  explicit adopt_lock_t() = default;
};

inline constexpr defer_lock_t defer_lock{};
inline constexpr try_to_lock_t try_to_lock{};
inline constexpr adopt_lock_t adopt_lock{};

template<class _Mutex>
class unique_lock {
public:
  typedef _Mutex mutex_type;

  _KSTD_API unique_lock() noexcept : __mutex_(nullptr), __owns_(false) {}

  _KSTD_API explicit unique_lock(mutex_type &__m) : __mutex_(&__m), __owns_(true) {
    __mutex_->lock();
  }

  _KSTD_API unique_lock(mutex_type &__m, defer_lock_t) noexcept : __mutex_(&__m), __owns_(false) {}

  _KSTD_API unique_lock(mutex_type &__m, try_to_lock_t) : __mutex_(&__m), __owns_(__m.try_lock()) {}

  _KSTD_API unique_lock(mutex_type &__m, adopt_lock_t) noexcept : __mutex_(&__m), __owns_(true) {}

  _KSTD_API unique_lock(unique_lock &&__other) noexcept
      : __mutex_(kstd::exchange(__other.__mutex_, nullptr)),
        __owns_(kstd::exchange(__other.__owns_, false)) {}

  _KSTD_API unique_lock &operator=(unique_lock &&__other) noexcept {
    if (this != &__other) {
      if (__owns_) __mutex_->unlock();

      __mutex_ = kstd::exchange(__other.__mutex_, nullptr);
      __owns_ = kstd::exchange(__other.__owns_, false);
    }
    return *this;
  }

  unique_lock(const unique_lock &) = delete;
  unique_lock &operator=(const unique_lock &) = delete;

  _KSTD_API ~unique_lock() {
    if (__owns_) __mutex_->unlock();
  }

  _KSTD_API void lock() {
    __mutex_->lock();
    __owns_ = true;
  }

  _KSTD_API bool try_lock() {
    __owns_ = __mutex_->try_lock();
    return __owns_;
  }

  _KSTD_API void unlock() {
    __mutex_->unlock();
    __owns_ = false;
  }

  _KSTD_API void swap(unique_lock &__other) noexcept {
    kstd::swap(__mutex_, __other.__mutex_);
    kstd::swap(__owns_, __other.__owns_);
  }

  _KSTD_API mutex_type *release() noexcept {
    mutex_type *__ret = __mutex_;
    __mutex_ = nullptr;
    __owns_ = false;
    return __ret;
  }

  [[__nodiscard__]] _KSTD_API mutex_type *mutex() const noexcept { return __mutex_; }

  [[__nodiscard__]] _KSTD_API bool owns_lock() const noexcept { return __owns_; }

  _KSTD_API explicit operator bool() const noexcept { return __owns_; }

private:
  mutex_type *__mutex_;
  bool __owns_;
};

template<class _Mutex>
_KSTD_API inline void swap(unique_lock<_Mutex> &__lhs, unique_lock<_Mutex> &__rhs) noexcept {
  __lhs.swap(__rhs);
}

}// namespace kstd
