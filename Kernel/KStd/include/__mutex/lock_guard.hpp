#pragma once

#include <__config.hpp>
#include <__mutex/tag_types.hpp>

#if defined(__clang__)
#  define _KSTD_TSA(...) __attribute__((__VA_ARGS__))
#else
#  define _KSTD_TSA(...)
#endif

namespace kstd {

template<class _Mutex>
class _KSTD_TSA(__scoped_lockable__) lock_guard {
  public:
  typedef _Mutex mutex_type;

  private:
  mutex_type &m_;

  public:
  [[__nodiscard__]] _KSTD_API explicit lock_guard(mutex_type &__m) _KSTD_TSA(__acquire_capability__(__m))
      : m_(__m) {
    m_.lock();
  }

  [[__nodiscard__]] _KSTD_API lock_guard(mutex_type &__m, adopt_lock_t) _KSTD_TSA(__requires_capability__(__m))
      : m_(__m) {}
  _KSTD_API ~lock_guard() _KSTD_TSA(__release_capability__()) { m_.unlock(); }

  lock_guard(lock_guard const &) = delete;
  lock_guard &operator=(lock_guard const &) = delete;
};

}// namespace kstd
