#include <undos/abi.h>
#include <undoscrt/lifecycle.hpp>
#include <undoscrt/process.h>
#include <stdio.h>

#include "libc/backend.h"

extern "C" {
int main(int, char **);
int crt_initialize_stdio();
extern void (*__preinit_array_start[])();
extern void (*__preinit_array_end[])();
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
extern void (*__fini_array_start[])();
extern void (*__fini_array_end[])();
char **environ = nullptr;
void *__dso_handle = &__dso_handle;
}

namespace {
undos::crt::ExitCallbacks callbacks;
void (*finalize_shared)();
bool exiting;

void run_forward(void (**begin)(), void (**end)()) {
  for (auto p = begin; p != end; ++p) if (*p) (*p)();
}
}

extern "C" __attribute__((visibility("default")))
int __cxa_atexit(void (*function)(void *), void *argument, void *dso) {
  return callbacks.add(function, argument, dso);
}

extern "C" __attribute__((visibility("default"))) void __cxa_finalize(void *dso) {
  callbacks.finalize(dso);
}

extern "C" __attribute__((visibility("default"))) int atexit(void (*function)()) {
  return callbacks.add(function);
}

extern "C" [[noreturn]] void crt_immediate_exit(int code) {
  undos_libc_terminate(code);
}

extern "C" [[noreturn]] void crt_exit(int code) {
  if (exiting) crt_immediate_exit(code);
  exiting = true;
  callbacks.finalize();
  for (auto p = __fini_array_end; p != __fini_array_start;) {
    if (*--p) (*p)();
  }
  if (finalize_shared) finalize_shared();
  (void) fflush(nullptr);
  crt_immediate_exit(code);
}

extern "C" __attribute__((visibility("default"))) [[noreturn]] void exit(int code) {
  crt_exit(code);
}

extern "C" __attribute__((visibility("default"))) [[noreturn]] void _Exit(int code) {
  crt_immediate_exit(code);
}

extern "C" [[noreturn]] void crt_start(const UnDOSStartupInfoV1 *info) {
  if (!info || !undos::abi::valid_startup(*info)) {
    undos_libc_startup_error();
    crt_immediate_exit(255);
  }
  environ = reinterpret_cast<char **>(info->envp);
  finalize_shared = reinterpret_cast<void (*)()>(info->finalize_shared);
  // Missing streams are permitted; subsequent I/O reports an error.
  (void) crt_initialize_stdio();
  run_forward(__preinit_array_start, __preinit_array_end);
  if (info->initialize_shared) reinterpret_cast<void (*)()>(info->initialize_shared)();
  run_forward(__init_array_start, __init_array_end);
  crt_exit(main(static_cast<int>(info->argc), reinterpret_cast<char **>(info->argv)));
}
