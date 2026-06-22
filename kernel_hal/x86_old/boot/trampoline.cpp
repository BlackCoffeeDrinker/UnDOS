#include <stddef.h>
#include <stdint.h>

#include "multiboot.hpp"

#include <kernel/boot/boot_info.hpp>
#include <kernel/elf.hpp>
#include <strfmt.hpp>

// Declare the assembly trampoline signature
extern "C" [[noreturn]] void entry_trampoline(uint32_t entry, uint32_t boot_info, uint32_t stack_top);

namespace {
#define PAGE_ALIGN_UP(addr) (((addr) + 4095) & ~4095)
constexpr size_t MAX_GLOBAL_SYMBOLS = 1024;
constexpr size_t DEFAULT_STACK_SIZE = 16384;

// Fixed memory mapping zones for compilation deployment
constexpr uintptr_t KERNEL_VIRTUAL_BASE = 0xC0000000;
constexpr uintptr_t KERNEL_PHYSICAL_BASE = 0x01000000;// 16MB Mark Physical Base

uintptr_t g_hal_virtual_base = 0;
uintptr_t g_hal_virtual_end = 0;
uintptr_t g_hal_physical_base = 0;
uintptr_t g_kernel_virtual_end = 0;

// GOT Runtime Registry
struct module_got_t {
  multiboot_module_t *mod = nullptr;
  uintptr_t vaddr = 0;
  uintptr_t paddr = 0;
};

enum Port : uint16_t { COM1 = 0x3F8 };

void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
uint8_t inb(uint16_t port) {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

void init_serial() {
  outb(COM1 + 1, 0x00);// Disable all interrupts
  outb(COM1 + 3, 0x80);// Enable DLAB (set baud rate divisor)
  outb(COM1 + 0, 0x03);// Set divisor to 3 (38400 baud)
  outb(COM1 + 1, 0x00);// (hi byte)
  outb(COM1 + 3, 0x03);// 8 bits, no parity, one stop bit
  outb(COM1 + 2, 0xC7);// Enable FIFO, clear them, with 14-byte threshold
}

void write_char(char c) {
  // Wait for transmit buffer empty
  while ((inb(COM1 + 5) & 0x20) == 0) {}
  outb(COM1, static_cast<uint8_t>(c));
}

template<typename... Args>
void write_fmt(const char *fmt, Args... args) {
  kstd::format_dst(write_char, fmt, args...);
}

[[noreturn]] void panic(const char *msg) {
  write_fmt("PANIC: {}\n\r", msg);
  for (;;) {}
}

// Minimal standalone memory operations
void kmemcpy(void *dest, const void *src, size_t n) {
  const auto d = static_cast<char *>(dest);
  const auto s = static_cast<const char *>(src);
  for (size_t i = 0; i < n; i++) d[i] = s[i];
}

void kmemset(void *s, int c, size_t n) {
  const auto p = static_cast<char *>(s);
  for (size_t i = 0; i < n; i++) p[i] = static_cast<char>(c);
}

bool kstrcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *s1 == *s2;
}

bool kstrstr(const char *haystack, const char *needle) {
  if (!*needle) return true;
  while (*haystack) {
    const char *h = haystack;
    const char *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n) return true;
    haystack++;
  }
  return false;
}

size_t kstrlen(const char *s) {
  size_t len = 0;
  while (*s++) len++;
  return len;
}

// Helper to deduce the target backing storage address from a designated linked virtual address
uintptr_t translate_vaddr_to_paddr(uint32_t vaddr) {
  // If the address belongs to the HAL zone
  if (g_hal_virtual_base != 0 && vaddr >= g_hal_virtual_base) {
    return vaddr - g_hal_virtual_base + g_hal_physical_base;
  }
  // If the address belongs to the Kernel zone (including its trailing GOT)
  if (vaddr >= KERNEL_VIRTUAL_BASE) {
    return vaddr - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE;
  }
  return vaddr;
}

uintptr_t get_elf_load_vaddr(uint32_t module_start) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(module_start);
  const auto *phdr_table = reinterpret_cast<kernel::Elf32_Phdr *>(module_start + ehdr->e_phoff);
  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr_table[i].p_type == kernel::PT_LOAD) return phdr_table[i].p_vaddr;
  }

  panic("Stage 1.5: Failed to find PT_LOAD segment in ELF header");
  return 0;
}

// Global Symbol Runtime Registry
struct runtime_symbol_t {
  const char *name;
  uint32_t value;
};

runtime_symbol_t g_symbol_table[MAX_GLOBAL_SYMBOLS];
size_t g_symbol_count = 0;

void register_global_symbol(const char *name, uint32_t value) {
  if (g_symbol_count >= MAX_GLOBAL_SYMBOLS) return;
  g_symbol_table[g_symbol_count++] = {name, value};
}

uint32_t lookup_global_symbol(const char *name) {
  for (size_t i = 0; i < g_symbol_count; ++i) {
    if (kstrcmp(g_symbol_table[i].name, name)) {
      return g_symbol_table[i].value;
    }
  }

  write_fmt("Stage 1.5: Failed to find symbol: {}\n\r", name);
  panic("Stage 1.5: Failed to find symbol");
  return 0;
}

// Statically reserved translation arrays inside Stage 1.5 BSS
__attribute__((aligned(4096))) uint32_t boot_page_dir[1024];
__attribute__((aligned(4096))) uint32_t identity_page_table[1024];
__attribute__((aligned(4096))) uint32_t hal_page_table[1024];
__attribute__((aligned(4096))) uint32_t kernel_page_table[1024];


void collect_symbols_from_module(uintptr_t module_start, uintptr_t v_base_override = 0) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(module_start);
  const auto *shdr_table = reinterpret_cast<kernel::Elf32_Shdr *>(module_start + ehdr->e_shoff);

  // Locate the string table for section names
  const auto section_str_tab = reinterpret_cast<const char *>(module_start + shdr_table[ehdr->e_shstrndx].sh_offset);

  const kernel::Elf32_Shdr *symtab_sh = nullptr;
  const kernel::Elf32_Shdr *strtab_sh = nullptr;

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name = section_str_tab + shdr_table[i].sh_name;
    if (kstrcmp(name, ".symtab")) symtab_sh = &shdr_table[i];
    if (kstrcmp(name, ".strtab")) strtab_sh = &shdr_table[i];
  }

  if (!symtab_sh || !strtab_sh) return;

  const auto *symbols = reinterpret_cast<kernel::Elf32_Sym *>(module_start + symtab_sh->sh_offset);
  const auto strings = reinterpret_cast<const char *>(module_start + strtab_sh->sh_offset);
  const size_t num_symbols = symtab_sh->sh_size / sizeof(kernel::Elf32_Sym);
  const uintptr_t original_base = get_elf_load_vaddr(module_start);

  for (size_t i = 0; i < num_symbols; ++i) {
    // Log all defined, globally visible or weak symbols
    const uint8_t binding = symbols[i].st_info >> 4;
    if (symbols[i].st_shndx != 0 && (binding == 1 || binding == 2)) {
      const char *sym_name = strings + symbols[i].st_name;
      if (*sym_name != '\0') {
        uint32_t val = symbols[i].st_value;
        if (v_base_override) val = val - original_base + v_base_override;
        register_global_symbol(sym_name, val);
      }
    }
  }
}

void load_and_copy_segments(uintptr_t module_start, uintptr_t v_base_override = 0) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(module_start);
  const auto *phdr_table = reinterpret_cast<kernel::Elf32_Phdr *>(module_start + ehdr->e_phoff);
  const uintptr_t original_base = get_elf_load_vaddr(module_start);

  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr_table[i].p_type == kernel::PT_LOAD) {
      uintptr_t vaddr = phdr_table[i].p_vaddr;
      if (v_base_override) vaddr = vaddr - original_base + v_base_override;

      const uintptr_t phys_dest = translate_vaddr_to_paddr(vaddr);

      // Copy data segment load space
      kmemcpy(reinterpret_cast<void *>(phys_dest),
              reinterpret_cast<const void *>(module_start + phdr_table[i].p_offset),
              phdr_table[i].p_filesz);

      // Handle clearing uninitialized space allocations (.bss padding bounds)
      if (phdr_table[i].p_memsz > phdr_table[i].p_filesz) {
        kmemset(reinterpret_cast<void *>(phys_dest + phdr_table[i].p_filesz),
                0,
                phdr_table[i].p_memsz - phdr_table[i].p_filesz);
      }
    }
  }
}

void setup_got(multiboot_module_t *mod, module_got_t &got, uintptr_t v_base_override = 0) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(mod->mod_start);
  const auto *shdr_table = reinterpret_cast<kernel::Elf32_Shdr *>(mod->mod_start + ehdr->e_shoff);
  const auto section_str_tab = reinterpret_cast<const char *>(mod->mod_start + shdr_table[ehdr->e_shstrndx].sh_offset);

  const kernel::Elf32_Shdr *symtab_sh = nullptr;
  const kernel::Elf32_Shdr *strtab_sh = nullptr;

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name = section_str_tab + shdr_table[i].sh_name;
    if (kstrcmp(name, ".symtab")) symtab_sh = &shdr_table[i];
    if (kstrcmp(name, ".strtab")) strtab_sh = &shdr_table[i];
  }
  if (!symtab_sh || !strtab_sh) return;

  const auto *symbols = reinterpret_cast<kernel::Elf32_Sym *>(mod->mod_start + symtab_sh->sh_offset);
  const auto strings = reinterpret_cast<const char *>(mod->mod_start + strtab_sh->sh_offset);
  const size_t num_symbols = symtab_sh->sh_size / sizeof(kernel::Elf32_Sym);
  const uintptr_t original_base = get_elf_load_vaddr(mod->mod_start);

  got.mod = mod;
  got.vaddr = 0;

  for (size_t i = 0; i < num_symbols; i++) {
    if (kstrcmp(strings + symbols[i].st_name, "_GLOBAL_OFFSET_TABLE_")) {
      got.vaddr = symbols[i].st_value;
      if (v_base_override) {
        got.vaddr = got.vaddr - original_base + v_base_override;
      }
      break;
    }
  }

  if (got.vaddr == 0) {
    panic("Could not find _GLOBAL_OFFSET_TABLE_ for in-place GOT patching");
  }

  got.paddr = translate_vaddr_to_paddr(got.vaddr);
}

// Fixed Relocation Engine
void process_module_relocations(uint32_t module_start, const module_got_t &got, uintptr_t v_base_override = 0) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(module_start);
  auto *shdr_table = reinterpret_cast<kernel::Elf32_Shdr *>(module_start + ehdr->e_shoff);
  const auto section_str_tab = reinterpret_cast<const char *>(module_start + shdr_table[ehdr->e_shstrndx].sh_offset);
  const uintptr_t original_base = get_elf_load_vaddr(module_start);

  const kernel::Elf32_Shdr *symtab_sh = nullptr;
  const kernel::Elf32_Shdr *strtab_sh = nullptr;

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name = section_str_tab + shdr_table[i].sh_name;
    if (kstrcmp(name, ".symtab")) symtab_sh = &shdr_table[i];
    if (kstrcmp(name, ".strtab")) strtab_sh = &shdr_table[i];
  }

  if (!symtab_sh || !strtab_sh) return;

  const auto *local_syms = reinterpret_cast<kernel::Elf32_Sym *>(module_start + symtab_sh->sh_offset);
  const auto local_strings = reinterpret_cast<const char *>(module_start + strtab_sh->sh_offset);

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    if (shdr_table[i].sh_type == kernel::SHT_REL) {
      const uint32_t target_section_idx = shdr_table[i].sh_info;
      // Skip relocation if target section is not allocated in memory (e.g. .debug_info)
      if (!(shdr_table[target_section_idx].sh_flags & kernel::SHF_ALLOC)) continue;

      const auto *rel_array = reinterpret_cast<kernel::Elf32_Rel *>(module_start + shdr_table[i].sh_offset);
      const size_t rel_entries = shdr_table[i].sh_size / sizeof(kernel::Elf32_Rel);

      for (size_t r = 0; r < rel_entries; ++r) {
        const kernel::Elf32_Rel rel = rel_array[r];
        const uint32_t sym_idx = kernel::ELF32_R_SYM(rel.r_info);
        const uint32_t rel_type = kernel::ELF32_R_TYPE(rel.r_info);
        const kernel::Elf32_Sym &local_sym = local_syms[sym_idx];

        uint32_t target_resolved_vma = 0;
        const char *lookup_name = local_strings + local_sym.st_name;

        // Phase 1: Clean Address Resolution
        if (sym_idx == 0) {
          // Null symbol - st_value is 0. Handle if it's a relative relocation later.
          target_resolved_vma = 0;
        } else if (local_sym.st_shndx != 0) {
          target_resolved_vma = local_sym.st_value;
          if (v_base_override) {
            target_resolved_vma = target_resolved_vma - original_base + v_base_override;
          }
        } else {
          target_resolved_vma = lookup_global_symbol(lookup_name);
        }

        if (target_resolved_vma == 0 && rel_type != kernel::R_386_GOTPC && rel_type != kernel::R_386_RELATIVE) {
          write_fmt("Stage 1.5 LINK ERROR: Unresolved external symbol: {} (Idx={}, Type={})\n\r",
                    lookup_name, sym_idx, rel_type);
          panic("Stage 1.5 LINK ERROR");
        }

        // Phase 2: Derive patch context locations safely
        uintptr_t v_patch_addr = rel.r_offset;
        if (v_base_override) v_patch_addr = v_patch_addr - original_base + v_base_override;

        const uintptr_t patch_phys_target = translate_vaddr_to_paddr(v_patch_addr);
        auto *patch_location = reinterpret_cast<uint32_t *>(patch_phys_target);

        // Phase 3: Perform absolute, relative, or global pointer math modifications
        const uintptr_t load_offset = v_base_override ? (v_base_override - original_base) : 0;

        if (rel_type == kernel::R_386_32) {
          if (local_sym.st_shndx != 0) {
            *patch_location += load_offset;
          } else {
            *patch_location += target_resolved_vma;
          }
        } else if (rel_type == kernel::R_386_PC32 || rel_type == kernel::R_386_PLT32) {
          if (local_sym.st_shndx == 0) {
            *patch_location += (target_resolved_vma - load_offset);
          }
          // Internal PC-relative calls are invariant under linear translation
        } else if (rel_type == kernel::R_386_GOT32 || rel_type == kernel::R_386_GOT32X) {
          const auto offset = static_cast<int32_t>(*patch_location);
          const uintptr_t entry_vaddr = got.vaddr + offset;
          const uintptr_t entry_paddr = translate_vaddr_to_paddr(entry_vaddr);

          // In-place GOT: Instruction already has correct offset, just fill the entry
          *reinterpret_cast<uint32_t *>(entry_paddr) = target_resolved_vma;
        } else if (rel_type == kernel::R_386_RELATIVE) {
          *patch_location += load_offset;
        }
        // GOTOFF and GOTPC are invariant under linear translation of the entire module
      }
    }
  }
}

// Add this helper function inside the anonymous namespace in trampoline.cpp
uintptr_t calculate_elf_virtual_end(uintptr_t module_start) {
  const auto *ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(module_start);
  const auto *phdr_table = reinterpret_cast<kernel::Elf32_Phdr *>(module_start + ehdr->e_phoff);

  uintptr_t highest_vaddr = 0;

  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr_table[i].p_type == kernel::PT_LOAD) {
      if (const uintptr_t segment_end = phdr_table[i].p_vaddr + phdr_table[i].p_memsz;
          segment_end > highest_vaddr) {
        highest_vaddr = segment_end;
      }
    }
  }

  // Page-align (4KB) to ensure the kernel doesn't map partial pages
  return (highest_vaddr + 4095) & ~4095;
}

void find_modules(const multiboot_info_t *mbi, multiboot_module_t *&kernel_module, multiboot_module_t *&hal_module) {
  if (!(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count < 2) {
    panic("CRITICAL ERROR: Stage 1.5 requires at least 2 loaded GRUB modules!");
  }

  auto *mods = reinterpret_cast<multiboot_module_t *>(mbi->mods_addr);
  for (size_t i = 0; i < mbi->mods_count; ++i) {
    const auto cmdline = reinterpret_cast<const char *>(mods[i].cmdline);
    write_fmt("Stage 1.5: Found module {} at [0x{x} - 0x{x}]\n\r", cmdline, mods[i].mod_start, mods[i].mod_end);

    if (kstrstr(cmdline, "hal")) {
      hal_module = &mods[i];
    } else if (kstrstr(cmdline, "kernel")) {
      kernel_module = &mods[i];
    }
  }

  if (!hal_module || !kernel_module) {
    panic("CRITICAL ERROR: Failed to isolate explicit hal/kernel files inside module inputs.");
  }
}

void calculate_layout(multiboot_module_t *kernel_module, multiboot_module_t *hal_module, module_got_t &kernel_got, module_got_t &hal_got) {
  g_kernel_virtual_end = calculate_elf_virtual_end(kernel_module->mod_start);
  g_hal_virtual_base = g_kernel_virtual_end;

  const uintptr_t hal_original_base = get_elf_load_vaddr(hal_module->mod_start);
  const uintptr_t hal_original_end = calculate_elf_virtual_end(hal_module->mod_start);
  const uintptr_t hal_size = hal_original_end - hal_original_base;
  g_hal_virtual_end = PAGE_ALIGN_UP(g_hal_virtual_base + hal_size);
  g_hal_physical_base = KERNEL_PHYSICAL_BASE + (g_hal_virtual_base - KERNEL_VIRTUAL_BASE);

  setup_got(kernel_module, kernel_got, KERNEL_VIRTUAL_BASE);
  setup_got(hal_module, hal_got, g_hal_virtual_base);

  write_fmt("Stage 1.5: Memory Layout:\n\r");
  write_fmt("  Kernel: [0x{x} - 0x{x}] -> Phys 0x{x}\n\r", KERNEL_VIRTUAL_BASE, g_kernel_virtual_end, KERNEL_PHYSICAL_BASE);
  write_fmt("  HAL:    [0x{x} - 0x{x}] -> Phys 0x{x}\n\r", g_hal_virtual_base, g_hal_virtual_end, g_hal_physical_base);
  write_fmt("  Kernel GOT: vaddr=0x{x}, paddr=0x{x}\n\r", kernel_got.vaddr, kernel_got.paddr);
  write_fmt("  HAL GOT:    vaddr=0x{x}, paddr=0x{x}\n\r", hal_got.vaddr, hal_got.paddr);
}

void load_and_link(const multiboot_module_t *kernel_module, const multiboot_module_t *hal_module, const module_got_t &kernel_got, const module_got_t &hal_got) {
  // PASS 2: Read all public exports across binaries into our table space
  collect_symbols_from_module(kernel_module->mod_start, KERNEL_VIRTUAL_BASE);
  collect_symbols_from_module(hal_module->mod_start, g_hal_virtual_base);
  write_fmt("Stage 1.5: Object Registry aggregated {} global symbols.\n\r", g_symbol_count);

  // PASS 3: Physically deploy layout maps to target frames
  load_and_copy_segments(kernel_module->mod_start, KERNEL_VIRTUAL_BASE);
  load_and_copy_segments(hal_module->mod_start, g_hal_virtual_base);
  write_fmt("Stage 1.5: ELF segments expanded into physical memory.\n\r");

  // PASS 4: Patch instructions using link hooks
  process_module_relocations(kernel_module->mod_start, kernel_got, KERNEL_VIRTUAL_BASE);
  process_module_relocations(hal_module->mod_start, hal_got, g_hal_virtual_base);
  write_fmt("Stage 1.5: Relocations applied and modules cross-stitched.\n\r");
}

void setup_paging(uintptr_t stack_bottom, uintptr_t stack_top, uintptr_t boot_info_end) {
  kmemset(boot_page_dir, 0, sizeof(boot_page_dir));
  kmemset(identity_page_table, 0, sizeof(identity_page_table));
  kmemset(hal_page_table, 0, sizeof(hal_page_table));
  kmemset(kernel_page_table, 0, sizeof(kernel_page_table));

  // Map lower memory space identity lines (0 - 4MB) to preserve execution frame
  for (size_t i = 0; i < 1024; ++i) {
    identity_page_table[i] = (i * 4096) | 0x003;
  }
  boot_page_dir[0] = reinterpret_cast<uint32_t>(identity_page_table) | 0x003;

  auto map_virtual_range = [](uint32_t v_start, uint32_t v_end, uint32_t p_start) {
    // Align loop start down to page boundary
    for (uint32_t v = v_start & ~4095; v < v_end; v += 4096) {
      const uint32_t slot = v >> 22;
      const uint32_t entry = (v >> 12) & 0x3FF;

      uint32_t *pt = nullptr;
      if (slot == (KERNEL_VIRTUAL_BASE >> 22)) {
        pt = kernel_page_table;
      } else if (slot == (KERNEL_VIRTUAL_BASE >> 22) + 1) {
        pt = hal_page_table;
      } else {
        panic("Stage 1.5: Virtual range exceeds pre-allocated high-half page tables (8MB max)");
      }

      // Calculate physical address for this virtual page. 
      // Mapping is linear: p = p_start + (v - v_start).
      // Mask out any unaligned bits from p_start to prevent corrupting PTE flags.
      const uint32_t p = (p_start + (v - v_start)) & ~4095;
      pt[entry] = p | 0x003;
      boot_page_dir[slot] = reinterpret_cast<uint32_t>(pt) | 0x003;
    }
  };

  const uintptr_t hal_size = g_hal_virtual_end - g_hal_virtual_base;
  const uintptr_t stack_phys_start = PAGE_ALIGN_UP(g_hal_physical_base + hal_size);
  const uintptr_t boot_info_phys_start = PAGE_ALIGN_UP(stack_phys_start + (stack_top - stack_bottom));

  // Map everything sequentially
  map_virtual_range(KERNEL_VIRTUAL_BASE, g_kernel_virtual_end, KERNEL_PHYSICAL_BASE);
  map_virtual_range(g_hal_virtual_base, g_hal_virtual_end, g_hal_physical_base);

  // Map Stack and Boot Info separately on their own clean pages
  map_virtual_range(stack_bottom, stack_top, stack_phys_start);
  map_virtual_range(stack_top, boot_info_end, boot_info_phys_start);

  // Connect the recursive tracking entry loop into slot 1023
  boot_page_dir[1023] = reinterpret_cast<uint32_t>(boot_page_dir) | 0x003;

  write_fmt("Stage 1.5: Initializing hardware paging architecture tables...\n\r");
  write_fmt("  boot_page_dir:       0x{x}\n\r", reinterpret_cast<uintptr_t>(boot_page_dir));
  write_fmt("  identity_page_table: 0x{x}\n\r", reinterpret_cast<uintptr_t>(identity_page_table));
  write_fmt("  kernel_page_table:   0x{x}\n\r", reinterpret_cast<uintptr_t>(kernel_page_table));
  write_fmt("  hal_page_table:      0x{x}\n\r", reinterpret_cast<uintptr_t>(hal_page_table));

  // Push the configurations straight into the processor controls
  asm volatile("mov %0, %%cr3" : : "r"(boot_page_dir));
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000;// Ignite Paging Enable bit flag
  asm volatile("mov %0, %%cr0" : : "r"(cr0));
  write_fmt("Stage 1.5: Paging engaged.\n\r");
}

kernel::boot_info_t *fill_boot_info(const multiboot_info_t *mbi, uintptr_t kernel_stack_top, size_t space_needed, size_t memory_map_count) {
  auto *boot_info_ptr = reinterpret_cast<kernel::boot_info_t *>(kernel_stack_top);
  kmemset(boot_info_ptr, 0, sizeof(kernel::boot_info_t));
  boot_info_ptr->page_size = 4096;

  boot_info_ptr->kernel_physical_start = KERNEL_PHYSICAL_BASE;
  boot_info_ptr->kernel_physical_end = KERNEL_PHYSICAL_BASE + (g_kernel_virtual_end - KERNEL_VIRTUAL_BASE);
  boot_info_ptr->kernel_virtual_start = KERNEL_VIRTUAL_BASE;
  boot_info_ptr->kernel_virtual_end = g_kernel_virtual_end;
  boot_info_ptr->hal_virtual_start = g_hal_virtual_base;
  boot_info_ptr->hal_virtual_end = g_hal_virtual_end;

  boot_info_ptr->memory_map_count = memory_map_count;
  boot_info_ptr->memory_map = reinterpret_cast<kernel::memory_region_t *>(boot_info_ptr + 1);

  const size_t command_line_size = (mbi->flags & MULTIBOOT_INFO_CMDLINE) ? kstrlen(reinterpret_cast<const char *>(mbi->cmdline)) + 1 : 0;
  if (command_line_size > 0) {
    boot_info_ptr->command_line = reinterpret_cast<char *>(boot_info_ptr->memory_map + memory_map_count);
    kmemcpy(boot_info_ptr->command_line, reinterpret_cast<const char *>(mbi->cmdline), command_line_size);
  }

  // Populate structural allocations
  boot_info_ptr->memory_map[0].base = PAGE_ALIGN_UP(g_hal_virtual_end);
  boot_info_ptr->memory_map[0].length = DEFAULT_STACK_SIZE;
  boot_info_ptr->memory_map[0].type = kernel::MemoryRegionType::KERNEL_STACK;

  boot_info_ptr->memory_map[1].base = reinterpret_cast<uintptr_t>(boot_info_ptr);
  boot_info_ptr->memory_map[1].length = space_needed;
  boot_info_ptr->memory_map[1].type = kernel::MemoryRegionType::BOOTLOADER;

  if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
    uintptr_t mmap_ptr = mbi->mmap_addr;
    const uintptr_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
    int count = 2;

    while (mmap_ptr < mmap_end) {
      const auto *entry = reinterpret_cast<multiboot_mmap_entry *>(mmap_ptr);

      auto region_type = kernel::MemoryRegionType::RESERVED;
      if (entry->type == multiboot_mmap_entry_type::MULTIBOOT_MEMORY_AVAILABLE) {
        region_type = kernel::MemoryRegionType::AVAILABLE;
      } else if (entry->type == multiboot_mmap_entry_type::MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) {
        region_type = kernel::MemoryRegionType::ACPI_RECLAIM;
      }

      boot_info_ptr->memory_map[count++] = {
          entry->addr,
          entry->len,
          region_type};

      mmap_ptr += entry->size + sizeof(entry->size);
    }
  }

  write_fmt("Stage 1.5: boot_info structure populated at 0x{x}.\n\r", reinterpret_cast<uintptr_t>(boot_info_ptr));
  return boot_info_ptr;
}

}// namespace

// -------------------------------------------------------------------------
// RUNTIME MAIN EXECUTOR
// -------------------------------------------------------------------------

extern "C" void kernel_main(uint32_t mb_physical_addr) {
  init_serial();
  write_fmt("--------------------------------------------------------------------------------\n\r");
  write_fmt("UnDOS Stage 1.5\n\r");
  write_fmt("--------------------------------------------------------------------------------\n\r");
  write_fmt("Booting with mb_physical_addr = 0x{x}\n\r", mb_physical_addr);

  auto *mbi = reinterpret_cast<multiboot_info_t *>(mb_physical_addr);
  multiboot_module_t *hal_module = nullptr;
  multiboot_module_t *kernel_module = nullptr;

  find_modules(mbi, kernel_module, hal_module);

  module_got_t kernel_got{};
  module_got_t hal_got{};

  calculate_layout(kernel_module, hal_module, kernel_got, hal_got);

  load_and_link(kernel_module, hal_module, kernel_got, hal_got);

  // High-half calculations
  const auto kernel_stack_bottom = PAGE_ALIGN_UP(g_hal_virtual_end);
  const auto kernel_stack_top = kernel_stack_bottom + DEFAULT_STACK_SIZE;

  size_t memory_map_count = 2;// 1 for stack, 1 for boot info
  if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
    uintptr_t calc_ptr = mbi->mmap_addr;
    const uintptr_t calc_end = mbi->mmap_addr + mbi->mmap_length;
    while (calc_ptr < calc_end) {
      const auto *entry = reinterpret_cast<multiboot_mmap_entry *>(calc_ptr);
      memory_map_count++;
      calc_ptr += entry->size + sizeof(entry->size);
    }
  } else {
    memory_map_count = 3;
  }
  const size_t memory_region_size = sizeof(kernel::memory_region_t) * memory_map_count;
  const size_t command_line_size = (mbi->flags & MULTIBOOT_INFO_CMDLINE) ? kstrlen(reinterpret_cast<const char *>(mbi->cmdline)) + 1 : 0;
  const auto space_needed = PAGE_ALIGN_UP(sizeof(kernel::boot_info_t) + memory_region_size + command_line_size);
  const auto boot_info_end = kernel_stack_top + space_needed;

  setup_paging(kernel_stack_bottom, kernel_stack_top, boot_info_end);

  write_fmt("Stage 1.5: Stack: bottom=0x{x}, top=0x{x}, size={}\n\r", kernel_stack_bottom, kernel_stack_top, DEFAULT_STACK_SIZE);
  write_fmt("Stage 1.5: Boot Info: start=0x{x}, end=0x{x}, length={}\n\r", kernel_stack_top, boot_info_end, space_needed);

  auto *boot_info = fill_boot_info(mbi, kernel_stack_top, space_needed, memory_map_count);

  auto *kernel_ehdr = reinterpret_cast<kernel::Elf32_Ehdr *>(kernel_module->mod_start);
  uintptr_t entry_point = kernel_ehdr->e_entry;

  if (entry_point < KERNEL_VIRTUAL_BASE || entry_point >= g_kernel_virtual_end) {
    write_fmt("ERROR: Entry point 0x{x} is outside mapped range [0x{x}, 0x{x}]\n\r",
              entry_point, KERNEL_VIRTUAL_BASE, g_kernel_virtual_end);
    panic("CRITICAL ERROR: Kernel entry point is outside of mapped kernel space!");
  }

  write_fmt("Stage 1.5: Jumping to Kernel Core Entry at 0x{x}...\n\r", entry_point);

  entry_trampoline(entry_point, reinterpret_cast<uint32_t>(boot_info), kernel_stack_top);

  panic("Stage 1.5: Kernel Core Entry returned");
}
