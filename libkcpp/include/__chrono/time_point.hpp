
#pragma once

#include <__chrono/duration.hpp>
#include <__config.hpp>
#include <__type_traits/common_type.hpp>

namespace kstd {

namespace chrono {

template <class _Clock, class _Duration = typename _Clock::duration>
class time_point {
  static_assert(__is_duration_v<_Duration>, "Second template parameter of time_point must be a kstd::chrono::duration");

public:
  typedef _Clock                           clock;
  typedef _Duration                        duration;
  typedef typename duration::rep           rep;
  typedef typename duration::period        period;

private:
  duration __d_;

public:
  _KSTD_API constexpr time_point() : __d_(duration::zero()) {}
  _KSTD_API constexpr explicit time_point(const duration& __d) : __d_(__d) {}

  // conversions
  template <class _Duration2, enable_if_t<is_convertible<_Duration2, duration>::value, int> = 0>
  _KSTD_API constexpr time_point(const time_point<clock, _Duration2>& __t) : __d_(__t.time_since_epoch()) {}

  // observer

  [[__nodiscard__]] _KSTD_API constexpr duration time_since_epoch() const { return __d_; }

  // arithmetic

  _KSTD_API constexpr time_point& operator+=(const duration& __d) {
    __d_ += __d;
    return *this;
  }
  _KSTD_API constexpr time_point& operator-=(const duration& __d) {
    __d_ -= __d;
    return *this;
  }

  // special values

  [[__nodiscard__]] _KSTD_API static constexpr time_point min() noexcept { return time_point(duration::min()); }
  [[__nodiscard__]] _KSTD_API static constexpr time_point max() noexcept { return time_point(duration::max()); }
};

} // namespace chrono

template <class _Clock, class _Duration1, class _Duration2>
struct common_type<chrono::time_point<_Clock, _Duration1>, chrono::time_point<_Clock, _Duration2> > {
  typedef chrono::time_point<_Clock, typename common_type<_Duration1, _Duration2>::type> type;
};

namespace chrono {

// time_point ==

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator==(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return __lhs.time_since_epoch() == __rhs.time_since_epoch();
}

// time_point !=

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator!=(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return !(__lhs == __rhs);
}

// time_point <

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator<(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return __lhs.time_since_epoch() < __rhs.time_since_epoch();
}

// time_point >

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator>(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return __rhs < __lhs;
}

// time_point <=

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator<=(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return !(__rhs < __lhs);
}

// time_point >=

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr bool
operator>=(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return !(__lhs < __rhs);
}

// time_point + duration

template <class _Clock, class _Duration1, class _Rep2, class _Period2>
[[__nodiscard__]] _KSTD_API inline constexpr
time_point<_Clock, typename common_type<_Duration1, duration<_Rep2, _Period2> >::type>
operator+(const time_point<_Clock, _Duration1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef time_point<_Clock, typename common_type<_Duration1, duration<_Rep2, _Period2> >::type> _Tr;
  return _Tr(__lhs.time_since_epoch() + __rhs);
}

template <class _Rep1, class _Period1, class _Clock, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr
time_point<_Clock, typename common_type<duration<_Rep1, _Period1>, _Duration2>::type>
operator+(const duration<_Rep1, _Period1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return __rhs + __lhs;
}

// time_point - duration

template <class _Clock, class _Duration1, class _Rep2, class _Period2>
[[__nodiscard__]] _KSTD_API inline constexpr
time_point<_Clock, typename common_type<_Duration1, duration<_Rep2, _Period2> >::type>
operator-(const time_point<_Clock, _Duration1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef time_point<_Clock, typename common_type<_Duration1, duration<_Rep2, _Period2> >::type> _Tr;
  return _Tr(__lhs.time_since_epoch() - __rhs);
}

// time_point - time_point

template <class _Clock, class _Duration1, class _Duration2>
[[__nodiscard__]] _KSTD_API inline constexpr typename common_type<_Duration1, _Duration2>::type
operator-(const time_point<_Clock, _Duration1>& __lhs, const time_point<_Clock, _Duration2>& __rhs) {
  return __lhs.time_since_epoch() - __rhs.time_since_epoch();
}

// time_point_cast

template <class _ToDuration, class _Clock, class _Duration, enable_if_t<__is_duration_v<_ToDuration>, int> = 0>
[[__nodiscard__]] _KSTD_API inline constexpr time_point<_Clock, _ToDuration>
time_point_cast(const time_point<_Clock, _Duration>& __t) {
  return time_point<_Clock, _ToDuration>(chrono::duration_cast<_ToDuration>(__t.time_since_epoch()));
}

// system_clock
//
// NOTE: There is currently no way to obtain the actual system time (no `now()` is provided),
// as the kernel does not yet expose a mechanism to query the current wall-clock time.

struct system_clock {
  typedef microseconds                     duration;
  typedef duration::rep                    rep;
  typedef duration::period                 period;
  typedef chrono::time_point<system_clock> time_point;

  static constexpr bool is_steady = false;
};

typedef system_clock::time_point system_time;

} // namespace chrono

} // namespace kstd
