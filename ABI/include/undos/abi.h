#pragma once

#include <stddef.h>
#include <stdint.h>

#define UNDOS_APPLICATION_ABI_VERSION 1u
#define UNDOS_ABI_NOTE_TYPE 1u

/* Addresses are fixed-width even when tools inspect this structure on a host. */
typedef struct UnDOSStartupInfoV1 {
  uint32_t size;
  uint32_t version;
  uint32_t argc;
  uint32_t argv;
  uint32_t envp;
  uint32_t initialize_shared;
  uint32_t finalize_shared;
  uint32_t flags;
  uint32_t reserved[4];
} UnDOSStartupInfoV1;

#ifdef __cplusplus
static_assert(sizeof(UnDOSStartupInfoV1) == 48);
static_assert(offsetof(UnDOSStartupInfoV1, argv) == 12);

namespace undos::abi {
inline bool valid_startup(const UnDOSStartupInfoV1 &info) {
  if (info.size != sizeof(info) || info.version != UNDOS_APPLICATION_ABI_VERSION ||
      !info.argc || info.argc > 32 || !info.argv || !info.envp || info.flags) return false;
  for (const auto value : info.reserved) if (value) return false;
  return true;
}
}
#endif
