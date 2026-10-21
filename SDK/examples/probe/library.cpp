#include <undoscrt/heap.h>
#include <undoscrt/string.h>

extern "C" void sdk_record(unsigned);

namespace {
int value;
struct Lifetime {
  Lifetime() {
    auto *memory = static_cast<char *>(crt_malloc(16));
    if (!memory) { sdk_record(99); return; }
    memcpy(memory, "shared runtime", 15);
    value = strcmp(memory, "shared runtime") == 0 ? 73 : 0;
    crt_free(memory);
    sdk_record(1);
  }
  ~Lifetime() { sdk_record(5); }
} lifetime;
__attribute__((destructor)) void finalize() { sdk_record(7); }
}

extern "C" int sdk_value() { return value; }
