
#pragma once

#include <stddef.h>
#include <stdint.h>


#if __has_attribute(__no_sanitize__) && !defined(__GNUC__)
#define _NO_CFI __attribute__((__no_sanitize__("cfi")))
#else
#define _NO_CFI
#endif
