
#pragma once


#include <__config.hpp>

extern "C" [[noreturn]] __attribute__((visibility("default"))) void KE_RUNTIME_Panic(const char *msg);

namespace kstd {

[[noreturn]] inline void panic(const char* msg) {
  KE_RUNTIME_Panic(msg);
}

}
