
#pragma once
#include <__config.hpp>

namespace kstd {
template<class T>
struct decay {
  using type = __decay(T);
};

#ifdef __GNUC__
template<class T>
using decay_t = typename decay<T>::type;
#else
template<class T>
using decay_t = __decay(T);
#endif

template<class T>
using decay_t = decay_t<T>;
}// namespace kstd
