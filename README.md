# UnDOS

UnDOS is a lightweight, modular operating system for Intel 386, 486, and
Pentium processors. It is written primarily in modern C++, with a focus on
simplicity, efficiency, and a familiar graphical environment inspired by
early Windows.

The kernel and hardware abstraction layer (HAL) are separate components.
Native applications use ELF binaries and an UnDOS SDK with CMake integration,
compiler drivers, and an initial C library subset. Program Manager and a
windowed terminal provide the graphical desktop.

UnDOS is under active development. It does not provide complete ISO C/POSIX,
hosted C++, or Windows binary compatibility.

## Goals

- Build a modern, modular operating system for retro x86 hardware, from the
  386 onward.
- Keep the kernel platform-independent, with hardware-specific operations
  behind the HAL. Future architecture goals include ARM32, PPC, VAX, and MIPS.
- Make application development straightforward from macOS, Windows, and Linux,
  without requiring applications to supply startup code or linker scripts.
- Deliver a lightweight graphical environment with predictable keyboard and
  mouse interaction.
- Support classic DOS-era software through future compatibility layers.

## Documentation

| Topic | Documentation |
|---|---|
| Build, run, and develop | [Build commands, repository layout, CLion/Docker setup, and bounded GUI workflow](Docs/Development.md) |
| Architecture | [Kernel, HAL, boot pipeline, memory layout, and handover](Docs/Architecture.md) |
| Desktop | [Program Manager, terminal, display configuration, and font infrastructure](Docs/Desktop.md) |
| Application development | [SDK, Docker image, CMake package, and compiler drivers](Docs/SDK.md) |
| Application contract | [Application ABI v1](Docs/ApplicationABI.md) |
| C library | [Supported functions, limits, streams, and lifecycle](Docs/Libc.md) |
| Kernel subsystems | [Subsystem overview](SUBSYSTEMS.md) and [loadable subsystem design](DYNAMIC_SUBSYSTEM.md) |
| System calls | [Syscall IDL and generation](SYSCALLS_IDL.md) |
| Plans | [SDK roadmap](SDK_ROADMAP.md), [kernel roadmap](Docs/KernelRoadmap.md), [Program Manager plan](TO_PROGRMAN.md), and [font plan](TTF_SPLIT_USER_KERNEL.md) |
| Known issues | [Bug tracker](BUGS.md) |
| Contribution constraints | [Development rules](Docs/Development.md#code-generation-invariant-rules), [application ABI](Docs/ApplicationABI.md), and [SDK contracts](Docs/SDK.md) |
