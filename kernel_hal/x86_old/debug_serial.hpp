
#pragma once

#include <stdint.h>

namespace kernel::x86::early {
enum Port : uint16_t { COM1 = 0x3F8 };

void init_serial();
void write_char(char c);

}// namespace kernel::x86::early
