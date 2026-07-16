# UnDOS Subsystems Overview

Here is a comprehensive overview of the core subsystems in the UnDOS project. This guide is designed to help new contributors and automated agents understand the architecture, internal mechanics, and primary APIs of each system.

---

### Object Manager
The Object Manager is the central registry for the kernel, implementing a hierarchical namespace (similar to the Windows NT Object Manager). It manages the lifecycle of kernel resources natively.

**How it Works:**
- **`KObject` Base:** All trackable resources (Directories, Threads, Drivers, Devices, Files) derive from a unified `kernel::KObject` base structure.
- **Reference Counting:** Each `KObject` has an atomic reference count. Lifecycles are automatically managed by the smart pointer `kernel::KObjectPtr<T>`.
- **Intrusive AVL Trees:** Objects are organized in memory using intrusive AVL trees (`adt::AvlNode<KObject>`), preventing the need for extra heap allocations during insertion.
- **Hierarchy:** During boot, an object tree is established with `\<Root>` at the top. Core directories like `\Device`, `\Driver`, `\Memory`, `\Threads`, and `\VFS` branch off it.

**Primary API (`kernel_api/include/kernel/object_manager.hpp`):**
- `KE_OB_InsertObject(parent, child)`: Inserts a child object into a directory object.
- `KE_OB_LookupObject(path)`: Resolves an absolute path (e.g., `\Device\Keyboard`) into its corresponding `KObjectPtr<KObject>`.
- `KE_OB_LookupObjectOfType<T>(path)`: Type-safe lookup that casts the result and returns `nullptr` if the type doesn't match.
- `KE_OB_GetRootDirectory()`, `KE_OB_GetDeviceDirectory()`, etc.: Global getters for standard system directories.

---

### VMM (Virtual Memory Manager)
The VMM subsystem handles platform-agnostic virtual address space allocation and tracking, along with the kernel heap.

**How it Works:**
- **Address Spaces:** Represented by the `AddressSpace` class (`kernel_api/include/kernel/virtual_memory.hpp`). Holds the architecture-specific translation root (e.g., `CR3`).
- **VADs (Virtual Address Descriptors):** Track reserved virtual memory regions. They act as nodes in an intrusive AVL tree (`VadTree`).
- **SLAB Allocator:** A high-performance SLAB allocator backs the kernel heap, providing fixed-size blocks (16 bytes up to 2048 bytes). It automatically backs the standard C++ `new` and `delete` operators for the kernel.
- **HAL Delegation:** The VMM manages virtual ranges, but delegates physical frame allocation and page table manipulation to the HAL.

**Primary API (`kernel_api/include/kernel/virtual_memory.hpp`):**
- `KE_VMM_AllocateRegion(as, size, flags)`: Reserves a contiguous virtual address region within a given `AddressSpace`.
- `KE_VMM_AllocateUserData(as, size)` / `KE_VMM_AllocateUserProcess(...)`: High-level routines that allocate physical frames, reserve a virtual region, and map the frames with appropriate protections.
- `KE_VMM_MapPhysical(as, virt, phys, flags)`: Directly maps a specific virtual address to a physical address.
- `KE_Malloc(size)` / `KE_Free(ptr)`: Dynamically allocates/frees memory from the kernel's SLAB caches.

---

### Scheduler and Threads
The Scheduler implements a platform-agnostic, priority-based, round-robin multitasking system.

**How it Works:**
- **`KThreadObject`:** Threads are represented as kernel objects (`KObject`). They store the execution state (`Ready`, `Running`, `Blocked`, `Terminated`), priority, remaining quantum, and stack boundaries.
- **Ready Queue:** An intrusive queue prioritizing higher priority values while maintaining FIFO order within the same priority level.
- **Preemption:** Driven by timer interrupts (`sched::tick()`). When a thread's time quantum exhausts, `schedule_next()` swaps context.
- **Context Switching:** CPU-specific saving/restoring of registers is deferred to the HAL (`HAL_CPU_SwitchContext`).

**Primary API (`kernel_api/include/kernel/scheduler.hpp`):**
- `KE_SCHED_CreateThread(entry, arg, priority)`: Allocates a new thread, creates its stack, and places it into the ready queue.
- `KE_SCHED_Yield()`: Voluntarily yields the CPU.
- `KE_SCHED_Block()` / `KE_SCHED_Unblock(thread)`: Transitions threads in and out of the `Blocked` state.
- `KE_SCHED_GetCurrentThread()`: Retrieves the `KThreadObject` pointer to the executing thread.

---

### Plug and Play (PnP) and Drivers
The PnP and Driver subsystems are responsible for loading driver executables, discovering hardware dynamically, and managing device lifecycles.

**How it Works:**
- **Driver Loader:** Dynamically loads ELF binaries (`KE_DRIVER_Load`). It parses headers, allocates executable memory, maps segments, applies relocations against the kernel symbol table, executes the driver entry point, and registers it.
- **Device Objects:** Hardware is modeled via `KDevice` nodes. Physical Device Objects (PDOs) represent raw hardware detected by a bus, while Functional Device Objects (FDOs) are created by a driver to actually control the device.
- **PnP Manager:** Coordinates the matching of drivers to hardware. When a bus enumerates a new PDO, the manager queries its Hardware ID, finds a matching driver, calls `driver->AddDevice(pdo)` to attach an FDO, assigns resources, and calls `StartDevice()`.

**Primary API (`kernel_api/include/kernel/driver.hpp`, `kernel_api/include/kernel/pnp.hpp`):**
- `KE_DRIVER_Load(path)`: Loads, relocates, and starts a driver module.
- `KE_PNP_ReportNewDevice(parent, pdo)`: Reports a discovered device to the PnP manager.
- `KE_PNP_EnumerateBus(busDevice)`: Triggers enumeration of a bus controller by calling its driver's `EnumerateDevices` callback.
- `KE_PNP_RegisterDeviceInterest(type, driver)`: Registers a driver as an upper filter for specific device classes (e.g., Disk).

---

### VFS (Virtual File System)
The VFS abstracts file system operations and natively integrates with the Object Manager namespace.

**How it Works:**
- **Mount Points:** Represented as `KVolumeMountObject`s residing under the `\VFS` directory.
- **Lookup & Delegation:** When a file is requested, the VFS finds the longest matching volume mount via the Object Manager, extracts the localized path, creates a `KFileObject`, and delegates the actual handle creation (`CreateHandle`) to the file system driver. This allows file systems to populate driver-specific contexts (`driverExtension`) transparently.

**Primary API (`kernel_api/include/kernel/vfs.hpp`):**
- `KE_VFS_RegisterFilesystemDriver(driver, name)` / `KE_VFS_UnregisterFilesystemDriver(filesystem)`: Registers/unregisters volume mapping drivers.
- `KE_VFS_OpenFile(path, mode)`: Resolves the object manager path to an active mount point, creates a `KFileObject`, and binds a driver handle.
- `KE_VFS_ReadFile(file, buffer)` / `KE_VFS_WriteFile(file, buffer)`: Reads/writes bytes to a `kstd::span<uint8_t>` buffer sequentially.
- `KE_VFS_SeekFile(file, absolute_offset)`: Modifies the internal read/write cursor.
- `KE_VFS_CloseFile(file)`: Finalizes the handle and drops references.

---

### HAL (Hardware Abstraction Layer)
The HAL subsystem serves as the boundary interface between the platform-agnostic UnDOS kernel core and the underlying hardware architecture.

**How it Works:**
- **Isolation:** By delegating all CPU, platform, and memory-specific implementations to the HAL, the core kernel remains highly portable.
- **C-Style ABI:** Exposes a clean C-style ABI grouped by domain (`PLATFORM`, `CPU`, `IO`, `PMM`, `VMM`).
- **x86 Legacy Targeting:** The current `hal_x86_old` static library targets 32-bit legacy x86 architectures without PAE, SMP, or ACPI.

**Primary API (`kernel_api/include/kernel/hal_interface.hpp`):**
- **Platform/CPU:** `HAL_PLATFORM_Init()`, `HAL_CPU_InitThreadContext()`, `HAL_CPU_SwitchContext()`, `HAL_CPU_EnableInterrupts()`.
- **I/O:** `HAL_IO_Out8/16/32()`, `HAL_IO_In8/16/32()`.
- **Physical Memory (PMM):** `HAL_PMM_AllocateFrames()`, `HAL_PMM_FreeFrames()`.
- **Virtual Memory (VMM):** `HAL_VMM_MapPage()`, `HAL_VMM_SwitchAddressSpace()`.

---

### libkcpp (Kernel C++ Standard Library)
`libkcpp` is a custom, freestanding C++ standard library implementation designed specifically for the UnDOS kernel environment.

**How it Works:**
- **No OS Dependencies:** Provides modern C++ core language features without relying on exceptions, RTTI, standard file I/O, or default OS heap allocators.
- **CMake Interface:** Structured as a CMake `INTERFACE` library, primarily utilizing headers and templates.
- **`kstd` Namespace:** Facilities are scoped under `kstd` instead of `std` to strictly separate them from standard hosted environments.

**Features & Primary API (`libkcpp/`):**
- **Memory:** `kstd::unique_ptr`, `kstd::allocator_traits`.
- **Data Views & Utilities:** `kstd::span`, `kstd::string_view`, `kstd::optional`, `kstd::expected`, `kstd::tuple`, `kstd::array`.
- **Kernel Strings:** Custom strings like `kstd::static_string` and `kstd::buffer_string` avoid dynamic heap allocations typically found in `std::string`.
- **Concepts & Type Traits:** `<concepts.hpp>` and `<type_traits.hpp>` support modern template metaprogramming.
- **Chrono:** Time utilities like `kstd::chrono::duration`.
