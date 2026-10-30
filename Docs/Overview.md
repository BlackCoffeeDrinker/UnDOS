# UnDOS system overview

[Project overview](../README.md)

UnDOS is a modular x86 operating system for the 386, 486, and early Pentium.
Its kernel owns portable services; the Hardware Abstraction Layer (HAL) and
hardware drivers handle platform-specific operations. Applications use the
UnDOS SDK and a familiar `main()` entry point. CMake consumers can use
`find_package(UnDOS)`; ordinary applications do not supply linker scripts or
special flags. See the [SDK guide](SDK.md) for the application build path.

The development goal is a short, predictable edit-build-test loop. This matters
on the priority CPUs, where moving files to the machine for a long test cycle is
slow. Keep boundaries clear and complexity inside the kernel, SDK, and build
tools rather than requiring application authors to manage kernel details.

Kernel development uses C++ ownership wrappers and distinct virtual and
physical address types to reduce lifetime and address-space mistakes. See
[kernel memory and ownership](Development.md#kernel-memory-and-ownership) for
`VirtualAddress`, `PhysicalAddress`, `KObjectPtr`, and the currently non-owning
`KDevicePtr`.

## Follow one boot to an application

The current image uses GRUB's `undos_direct` module to hand the kernel and HAL
images to the kernel entry. `kernel_core_main` initializes the HAL and kernel
managers, mounts an early read-only boot volume, starts boot drivers, discovers
native volumes, and mounts the selected root. It registers `NativeElfLoader`
before the intended final step: start `/System/init.elf`. The bootloader route
is in flux: the owner is investigating a GRUB module instead of Multiboot and 
plans a bootloader of their own. See [current boot order](Architecture.md#current-boot-path).

```mermaid
flowchart LR
    G[GRUB direct loader] --> K[Kernel entry]
    K --> H[HAL]
    H --> M[Kernel managers]
    M --> P[PnP and drivers]
    P --> V[Volumes and VFS]
    V --> R[Register NativeElfLoader]
    R --> D[Temporary debugging stop]
    D -. after removing debug stop .-> E[ELoader starts init]
    E --> A[Application and CRT]
```

## Component responsibilities

| Component | Responsibility |
|---|---|
| HAL | Provides the hardware-specific CPU, memory, interrupt, timer, and I/O boundary. |
| Object Manager | Owns typed kernel objects and the system namespace used to find them. |
| Resource Manager | Reserves fixed device resource grants, detects conflicts, and lets drivers query grants. |
| VolMgr | Turns disk partition ranges into Volume devices and forwards bounded I/O to the parent disk. |
| Subsystem | Loads resident relocatable kernel modules, their dependencies and exports, and configured automatic starts. |
| ELoader | Loads a user executable and its shared objects, relocates them, then transfers control to the executable entry. |

These names describe UnDOS responsibilities, not Windows compatibility. UnDOS
uses familiar Win32-style application concepts, but it does not promise Windows
binary compatibility. For PnP's NT-inspired vocabulary and its implementation
differences, see [Plug and Play](PnP.md).

## See also

- [Architecture and boot order](Architecture.md)
- [Storage and root mounts](Storage.md)
- [Plug and Play](PnP.md)
- [Process startup](ProcessStartup.md)
- [Application SDK](SDK.md)
- [Kernel object definitions](../Kernel/API/include/kernel/device.hpp)
