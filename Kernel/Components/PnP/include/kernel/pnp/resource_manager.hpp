
#pragma once

namespace kernel::resource {
// Initializes the kernel resource manager registry. Call once at boot, right
// after KE_PNP_Init(), before any device is reported.
void init();
}// namespace kernel::res
