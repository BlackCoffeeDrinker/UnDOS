# Kernel global constructors

This page explains how the kernel image and the kernel modules (drivers and subsystems) run C++ global constructors, and what those constructors may do.

## Policy

- Prefer constant initialization. Give the type a `constexpr` constructor and declare the global `constinit`. The compiler then rejects any global that would need code to run. Core objects such as `g_dpcCore` (`Kernel/Source/src/dispatch.cpp`) must use `constinit`.
- A dynamic initializer may only store values into its own object. It must not:
  - allocate memory;
  - log;
  - take a lock;
  - call the HAL;
  - read or call another global, because order across translation units is unspecified.
- Do stateful set-up in an explicit `init()` function that `kernel_core_main` calls in order.
- Destructors never run. The kernel is built with `-fno-use-cxa-atexit`, and `__cxa_atexit` is a no-op. Modules stay resident, so their `.dtors`/`.fini_array` are ignored.

## Kernel image

- `Kernel/Source/linker.ld` collects `.init_array` and `.ctors` between `__init_array_start` and `__init_array_end`.
- `kernel::crt::run_global_constructors()` (`Kernel/Source/src/crt_init.cpp`) is the first call in `kernel_core_main`, before `HAL_PLATFORM_Init`. It runs each entry once, in order, and skips `0`/`-1` sentinels.
- A marker constructor in `crt_init.cpp` proves the walk ran. If the marker is not set, the kernel panics right after `HAL_PLATFORM_Init`.

## Kernel modules

- Drivers and subsystems are loaded by `kernel::elf::LoadElfFromVfs`. They are `ET_EXEC` images linked with `--emit-relocs` and have no dynamic segment, so the loader finds their `.ctors` and `.init_array` sections by name in the section headers.
- `kernel::elf::RunInitializers` runs `.ctors`, then `.init_array`, after relocation and dependency loading and just before the entry point.
- Any table or entry outside the loaded image fails the load, and the entry point is not called.
- `RunInitializers` rejects images loaded for user mode. User images run their own constructors in ring 3.
- Module constructors follow the same policy. The kernel is fully running when they execute, but they should still do their set-up in the entry point.

## Checking an image

To list the constructors in a built image, use the i686-elf toolchain:

```sh
i686-elf-objdump -h kernel | grep -E 'init_array|ctors'
i686-elf-objdump -s -j .init_array kernel
```

Every entry is a global with a dynamic initializer. Each one should be made `constinit` or shown to follow the policy above.
