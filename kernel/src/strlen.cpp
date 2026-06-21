
#include "Core.hpp"

extern "C" size_t strlen(const char *str) {
  const char *s;

  for (s = str; *s; ++s)
    ;
  return static_cast<size_t>(s - str);
}
