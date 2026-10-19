
#pragma once

#include <kernel/__core.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObject.hpp>

namespace kernel {
struct KProcessObject;
struct ProcessStreams {
  KObjectPtr<KObject> input;
  KObjectPtr<KObject> output;
  KObjectPtr<KObject> error;
};
}

// Empty arguments preserve the boot-time CreateProcess(path) contract.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif
extern "C" UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KProcessObject>,
    KE_USER_CreateProcess,
    const kstd::string_view &exec_path, const kstd::string_view &arguments = {});
extern "C" UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KProcessObject>,
    KE_USER_CreateProcessWithStreams,
    const kstd::string_view &exec_path, const kstd::string_view &arguments,
    const kernel::ProcessStreams &streams);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
