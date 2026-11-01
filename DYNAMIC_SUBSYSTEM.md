# Dynamic subsystems

Goal: keep the boot image small by loading optional subsystems (networking first, later GFX) as relocatable kernel-space ELF modules, the way drivers are loaded today.

## Phases

| Phase | Scope                                                                                                                                                                                        | Status      |
|-------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------|
| 1     | ELF-note module metadata, `KModuleObject` export table, subsystem loader, resolver that walks modules, `add_subsystem()` CMake helper, `network` skeleton, registry entry and image staging  | Implemented |
| 2     | Register `Kernel` and `HAL` as modules built from `boot_symbols`; replace the flat `SymbolTable` and drop the 528 KiB boot symbol array in favour of a packed blob                           | Implemented |
| 3     | Syscall `provider` attribute in `ABI/IDL/syscalls.txt`, generated slot table `{provider, symbol, handler}` for module-provided calls; empty slot returns Unsupported (demand-loading deferred) | Implemented |
| 4     | Module unload (never for syscall providers at first), dependency pinning                                                                                                                     | Planned     |

## Phase 1 design

### Module metadata
There is no descriptor struct; the image declares itself through standard ELF data:

- **Name and dependencies:** a `.note.undos.module` note (owner `UnDOS`, type 1) emitted by `add_subsystem`. Its descriptor is `<name>\0<dependency>\0...\0`. The name is the CMake target name.
- **Exports:** the loader reads `.symtab` and keeps defined, `GLOBAL`, default-visibility `FUNC`/`OBJECT` symbols. Modules build with `-fvisibility=hidden`, so only symbols marked `UNDOS_MODULE_EXPORT` are visible. The image must not be stripped.
- **Entry:** `SubsystemEntry(const KObjectPtr<KModuleObject> &)`, called once the module is registered (same shape as a driver entry).

### Kernel side
- `KModuleObject` holds `load_base`, `total_size`, the dependency names, the export list and the entry point. Exports are searched linearly per module (tens of symbols, not thousands); sort or hash later if needed.
- `KSubsystemObject` keeps its user-mode loader fields and gains a reference to its `KModuleObject`.
- `Kernel/Source/src/subsystem_loader.cpp` provides `KE_SUBSYSTEM_Load(path)`:
  1. Load the ELF with `relocatable = true`. Resolution walks every registered module's export table (`Kernel`, `HAL`, then loaded subsystems) through `module::Resolve`.
  2. Reject images without the note, with a duplicate module name, or exporting a symbol that already exists.
  3. Load missing `dependencies` first, using the registry `/Subsystems/<name>/ImagePath` (depth limited to detect cycles).
  4. Insert the `KModuleObject` and `KSubsystemObject`, then call `SubsystemEntry`.
- Registry: `/Subsystems/<name>/{Start,ImagePath}`; `Start=Automatic` modules load right after the Automatic driver pass.
- Policy: duplicate export names fail the load; an unresolved import fails the load; modules stay resident (no unload yet).

### Build
`BuildSupport/AddSubsystem.cmake`:

```cmake
add_subsystem(network
    OUTPUT_NAME net          # <= 8 chars, FAT short name rule
    START Automatic
    SOURCES net.cpp)
```

- `DEPENDS` lists other subsystem targets and is written into the note.
- Builds like `add_driver` with `Kernel/Subsystems/subsystem.ld` (entry `SubsystemEntry`).
- Registers `<TARGET>` in the global property `UNDOS_SUBSYSTEMS` with its output name; `make-initial-disk` copies each to `System/Subsys/<OUTPUT_NAME>.elf`.
- Passes the registry entries to `config.py author-localmachine` (generated list file).

### Validation
Bounded QEMU run (`./debug-and-run-for-15-seconds.sh`) must show the network subsystem loading, and hosted tests must still pass.

## Phase 2 design
- `BootInfoT` carries `{name_offset, address}` entries plus one packed NUL-terminated name pool (1024 symbols, 48 KiB), with `boot_kernel_symbol_count` splitting Kernel from HAL symbols. Boot info shrank from 133 to 15 reclaimed pages.
- `kernel::ExportTable` (`Kernel/API/.../module/export_table.hpp`) stores a packed name blob and a name-sorted entry array; lookup is a binary search with exact comparison (no hashing, so no collisions). `Seal()` sorts and rejects duplicates.
- `kernel::module` registry (`Kernel/Source/src/module/module_registry.*`) owns the module list, builds `Kernel`/`HAL` `KModuleObject`s from the boot symbols, rejects duplicate module names or exports, and publishes modules under `\Modules`.
- The flat `SymbolTable` is removed; `driver_loader` and `subsystem_loader` resolve through `module::Resolve`.
- Hosted coverage: `export_table_test.cpp` (`[export-table]`). The hosted link is currently blocked by unrelated unused-variable errors in `volume_manager_test.cpp` and `configuration_manager_test.cpp`.

## Phase 3 design
- IDL: `syscall V1 NET Name [500] provider <module> { ... };`. Kernel-provided syscalls are unchanged (`ABI/IDL/netsyscall.txt` holds the first group, 500-599).
- `Tools/create-syscalls.py` emits a `ProviderSlot g_providerSlots[]` table; each provider syscall's dispatch reads its slot by a generation-time constant index (one load and a null check, no lookup), so the cost over a built-in call is one indirect call.
- `kernel::syscall::BindProvider(module)` (called by the subsystem loader after `module::Register`) looks up the exported `handle_<Name>_<Version>` of every syscall the module provides and binds them all-or-nothing (`NotFound` if one is missing, `AlreadyExists` if bound). An unbound slot yields `UNSUPPORTED`.
- Modules include the generated `__kernel_types.h` (`add_subsystem` adds the include paths) and export `UNDOS_MODULE_EXPORT kstd::error_code handle_<Name>_<Version>(__KRN_<GROUP>_<NAME>_<VER> &&)`.
- First user: `QueryNetworkVersion` (500), implemented by `Kernel/Subsystems/network`, called from the ABI probe. Verified at runtime: the handler is reached and returns version 1.
- Verified with `Tools/progman-smoke.py --scenario abi` (all four probe lines PASS, including `QueryNetworkVersion`). An earlier probe failure was unrelated: `StandardStreamUnavailable` mapped to `UNKNOWN` instead of the documented `INVALID_ARGUMENT`, fixed in `KErrorToSysReturnCode.cpp`.

## Open items
- Syscall slot-table design (phase 3).
- Policy on demand-loading providers when a slot is empty.
- Unload semantics for syscall providers (386/486 lack cheap atomics for per-call pinning).
