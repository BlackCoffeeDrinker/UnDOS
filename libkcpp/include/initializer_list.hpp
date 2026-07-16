
#pragma once

#include <__config.hpp>

namespace kstd {

template<class _Ep>
class initializer_list {
  const _Ep *__begin_;
  size_t __size_;

  _KSTD_API constexpr initializer_list(const _Ep *__b, size_t __s) noexcept
      : __begin_(__b),
        __size_(__s) {}

  public:
  typedef _Ep value_type;
  typedef const _Ep &reference;
  typedef const _Ep &const_reference;
  typedef size_t size_type;

  typedef const _Ep *iterator;
  typedef const _Ep *const_iterator;

  _KSTD_API constexpr initializer_list() noexcept : __begin_(nullptr), __size_(0) {}

  [[__nodiscard__]] _KSTD_API constexpr size_t size() const noexcept {
    return __size_;
  }

  [[__nodiscard__]] _KSTD_API constexpr const _Ep *begin() const noexcept {
    return __begin_;
  }

  [[__nodiscard__]] _KSTD_API constexpr const _Ep *end() const noexcept {
    return __begin_ + __size_;
  }
};

template<class _Ep>
inline _KSTD_API constexpr const _Ep *begin(initializer_list<_Ep> __il) noexcept {
  return __il.begin();
}

template<class _Ep>
inline _KSTD_API constexpr const _Ep *end(initializer_list<_Ep> __il) noexcept {
  return __il.end();
}

}// namespace kstd
