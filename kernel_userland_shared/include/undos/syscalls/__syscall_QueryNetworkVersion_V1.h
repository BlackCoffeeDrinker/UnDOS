#pragma once

// Generated from UnDOS IDL

#include <stdint.h>
#include <stddef.h>

/** Reports the version of the network subsystem; UNSUPPORTED when it is not loaded. */
struct __SYS_NET_QUERYNETWORKVERSION_V1 {
    /** Interface version implemented by the network subsystem. */
    uint32_t version;
};

constexpr size_t   __SYS_NET_QUERYNETWORKVERSION_V1_SIZE = sizeof(struct __SYS_NET_QUERYNETWORKVERSION_V1);
constexpr uint32_t __SYS_NET_QUERYNETWORKVERSION_V1_CALLNUMBER = 500;

