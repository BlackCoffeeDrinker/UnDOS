Stage 1.5 for booting x86 under multiboot spec
Runs identity-mapped in lower memory or at a temporary high address.
Responsible for loading the HAL and the kernel, and passing control to HAL_PlatformInit

```c++
void stage15_main(multiboot_info_t* mbi) {
// 1. Initialize early paging structures (map 0x80000000 and 0xC0000000)
setup_higher_half_paging();

    // 2. Parse ELF headers and copy segments to their destination VMAs
    elf_module_t hal_mod    = load_elf_module(find_grub_module(mbi, "hal.elf"));
    elf_module_t kernel_mod = load_elf_module(find_grub_module(mbi, "kernel.elf"));

    // 3. PASS 1: Read the symbol tables of BOTH binaries and collect exports
    symbol_table_t global_registry;
    global_registry.absorb_exports(hal_mod);
    global_registry.absorb_exports(kernel_mod);

    // 4. PASS 2: Resolve undefined symbols (Imports)
    // For every undefined symbol in the HAL, look it up in the global registry (find Kernel symbols)
    resolve_imports(hal_mod, global_registry);
    // For every undefined symbol in the Kernel, look it up in the global registry (find HAL symbols)
    resolve_imports(kernel_mod, global_registry);

    // 5. Handshake: Locate the HAL entry point and leap!
    auto* hal_init = reinterpret_cast<void(*)()>(global_registry.lookup("HAL_PlatformInit"));
    
    hal_init();
}
```
