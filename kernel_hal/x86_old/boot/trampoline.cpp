#include <stddef.h>
#include <stdint.h>

#include "multiboot.hpp"

#include <kernel/boot/boot_info.hpp>
#include <strfmt.hpp>

namespace {
constexpr size_t MAX_MEMORY_REGIONS = 128;
constexpr size_t MAX_GLOBAL_SYMBOLS = 1024;

// Fixed memory mapping zones for compilation deployment
constexpr uintptr_t HAL_VIRTUAL_BASE = 0x80000000;
constexpr uintptr_t HAL_PHYSICAL_BASE = 0x00800000;// 8MB Mark Physical Base
constexpr uintptr_t KERNEL_VIRTUAL_BASE = 0xC0000000;
constexpr uintptr_t KERNEL_PHYSICAL_BASE = 0x01000000;// 16MB Mark Physical Base

enum Port : uint16_t { COM1 = 0x3F8 };


kernel::memory_region_t g_parsed_mmap[MAX_MEMORY_REGIONS];
size_t g_parsed_mmap_count = 0;

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

// -------------------------------------------------------------------------
// STANDALONE ELF AND SUBSYSTEM PRIMITIVES
// -------------------------------------------------------------------------

struct Elf32_Ehdr {
  unsigned char e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct Elf32_Phdr {
  uint32_t p_type;
  uint32_t p_offset;
  uint32_t p_vaddr;
  uint32_t p_paddr;
  uint32_t p_filesz;
  uint32_t p_memsz;
  uint32_t p_flags;
  uint32_t p_align;
};

struct Elf32_Shdr {
  uint32_t sh_name;
  uint32_t sh_type;
  uint32_t sh_flags;
  uint32_t sh_addr;
  uint32_t sh_offset;
  uint32_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint32_t sh_addralign;
  uint32_t sh_entsize;
};

struct Elf32_Sym {
  uint32_t st_name;
  uint32_t st_value;
  uint32_t st_size;
  unsigned char st_info;
  unsigned char st_other;
  uint16_t st_shndx;
};

struct Elf32_Rel {
  uint32_t r_offset;
  uint32_t r_info;
};

#define PT_LOAD 1
#define SHT_REL 9
#define ELF32_R_SYM(val) ((val) >> 8)
#define ELF32_R_TYPE(val) ((val) & 0xff)

// Minimal standalone memory operations
void kmemcpy(void *dest, const void *src, size_t n) {
  auto d = static_cast<char *>(dest);
  auto s = static_cast<const char *>(src);
  for (size_t i = 0; i < n; i++) d[i] = s[i];
}

void kmemset(void *s, int c, size_t n) {
  auto p = static_cast<char *>(s);
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

// Helper to deduce the target backing storage address from a designated linked virtual address
uintptr_t translate_vaddr_to_paddr(uint32_t vaddr) {
  if (vaddr >= KERNEL_VIRTUAL_BASE) {
    return vaddr - KERNEL_VIRTUAL_BASE + KERNEL_PHYSICAL_BASE;
  } else if (vaddr >= HAL_VIRTUAL_BASE) {
    return vaddr - HAL_VIRTUAL_BASE + HAL_PHYSICAL_BASE;
  }
  return vaddr;
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
  return 0;
}

// Statically reserved translation arrays inside Stage 1.5 BSS
__attribute__((aligned(4096))) uint32_t boot_page_dir[1024];
__attribute__((aligned(4096))) uint32_t identity_page_table[1024];
__attribute__((aligned(4096))) uint32_t hal_page_table[1024];
__attribute__((aligned(4096))) uint32_t kernel_page_table[1024];

// Executable trackers
multiboot_module_t *hal_module = nullptr;
multiboot_module_t *kernel_module = nullptr;

void collect_symbols_from_module(multiboot_module_t *mod) {
  const auto *ehdr = reinterpret_cast<Elf32_Ehdr *>(mod->mod_start);
  auto *shdr_table = reinterpret_cast<Elf32_Shdr *>(mod->mod_start + ehdr->e_shoff);

  // Locate the string table for section names
  auto section_str_tab = reinterpret_cast<const char *>(mod->mod_start + shdr_table[ehdr->e_shstrndx].sh_offset);

  const Elf32_Shdr *symtab_sh = nullptr;
  const Elf32_Shdr *strtab_sh = nullptr;

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name = section_str_tab + shdr_table[i].sh_name;
    if (kstrcmp(name, ".symtab")) symtab_sh = &shdr_table[i];
    if (kstrcmp(name, ".strtab")) strtab_sh = &shdr_table[i];
  }

  if (!symtab_sh || !strtab_sh) return;

  const auto *symbols = reinterpret_cast<Elf32_Sym *>(mod->mod_start + symtab_sh->sh_offset);
  auto strings = reinterpret_cast<const char *>(mod->mod_start + strtab_sh->sh_offset);
  const size_t num_symbols = symtab_sh->sh_size / sizeof(Elf32_Sym);

  for (size_t i = 0; i < num_symbols; ++i) {
    // Log all defined, globally visible symbols
    if (symbols[i].st_shndx != 0 && (symbols[i].st_info >> 4) == 1) {
      const char *sym_name = strings + symbols[i].st_name;
      if (*sym_name != '\0') {
        register_global_symbol(sym_name, symbols[i].st_value);
      }
    }
  }
}

void load_and_copy_segments(multiboot_module_t *mod) {
  const auto *ehdr = reinterpret_cast<Elf32_Ehdr *>(mod->mod_start);
  const auto *phdr_table = reinterpret_cast<Elf32_Phdr *>(mod->mod_start + ehdr->e_phoff);

  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr_table[i].p_type == PT_LOAD) {
      const uintptr_t phys_dest = translate_vaddr_to_paddr(phdr_table[i].p_vaddr);

      // Copy data segment load space
      kmemcpy(reinterpret_cast<void *>(phys_dest),
              reinterpret_cast<const void *>(mod->mod_start + phdr_table[i].p_offset),
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

void process_module_relocations(multiboot_module_t *mod) {
  const auto *ehdr = reinterpret_cast<Elf32_Ehdr *>(mod->mod_start);
  auto *shdr_table = reinterpret_cast<Elf32_Shdr *>(mod->mod_start + ehdr->e_shoff);
  auto section_str_tab = reinterpret_cast<const char *>(mod->mod_start + shdr_table[ehdr->e_shstrndx].sh_offset);

  const Elf32_Shdr *symtab_sh = nullptr;
  const Elf32_Shdr *strtab_sh = nullptr;

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    const char *name = section_str_tab + shdr_table[i].sh_name;
    if (kstrcmp(name, ".symtab")) symtab_sh = &shdr_table[i];
    if (kstrcmp(name, ".strtab")) strtab_sh = &shdr_table[i];
  }

  const auto *local_syms = reinterpret_cast<Elf32_Sym *>(mod->mod_start + symtab_sh->sh_offset);
  auto local_strings = reinterpret_cast<const char *>(mod->mod_start + strtab_sh->sh_offset);

  for (size_t i = 0; i < ehdr->e_shnum; ++i) {
    if (shdr_table[i].sh_type == SHT_REL) {
      const auto *rel_array = reinterpret_cast<Elf32_Rel *>(mod->mod_start + shdr_table[i].sh_offset);
      const size_t rel_entries = shdr_table[i].sh_size / sizeof(Elf32_Rel);

      for (size_t r = 0; r < rel_entries; ++r) {
        const Elf32_Rel rel = rel_array[r];
        const uint32_t sym_idx = ELF32_R_SYM(rel.r_info);
        const uint32_t rel_type = ELF32_R_TYPE(rel.r_info);

        uint32_t target_resolved_vma = 0;
        const Elf32_Sym local_sym = local_syms[sym_idx];

        if (local_sym.st_shndx != 0) {
          // Internal local relocation
          target_resolved_vma = local_sym.st_value;
        } else {
          // External cross-binary relocation lookup
          const char *lookup_name = local_strings + local_sym.st_name;
          target_resolved_vma = lookup_global_symbol(lookup_name);
          if (target_resolved_vma == 0) {
            write_fmt("Stage 1.5 LINK ERROR: Unresolved external object: {}\n\r", lookup_name);
            while (1);
          }
        }

        const uintptr_t patch_phys_target = translate_vaddr_to_paddr(rel.r_offset);
        auto patch_location = reinterpret_cast<uint32_t *>(patch_phys_target);

        if (rel_type == 1) {// R_386_32 Absolute mapping adjustments
          *patch_location += target_resolved_vma;
        } else if (rel_type == 2) {// R_386_PC32 Relative branch path execution
          *patch_location += (target_resolved_vma - rel.r_offset);
        }
      }
    }
  }
}

// Add this helper function inside the anonymous namespace in trampoline.cpp
uintptr_t calculate_elf_virtual_end(multiboot_module_t *mod) {
  const auto *ehdr = reinterpret_cast<Elf32_Ehdr *>(mod->mod_start);
  const auto *phdr_table = reinterpret_cast<Elf32_Phdr *>(mod->mod_start + ehdr->e_phoff);

  uintptr_t highest_vaddr = 0;

  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr_table[i].p_type == PT_LOAD) {
      const uintptr_t segment_end = phdr_table[i].p_vaddr + phdr_table[i].p_memsz;
      if (segment_end > highest_vaddr) {
        highest_vaddr = segment_end;
      }
    }
  }

  // Page-align (4KB) to ensure the kernel doesn't map partial pages
  return (highest_vaddr + 4095) & ~4095;
}

void parse_multiboot_memory_map(multiboot_info_t *mbi) {
  if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP)) {
    // Fallback: create a single safe region if GRUB didn't pass a map
    g_parsed_mmap[0] = {0, 0x01000000, kernel::MemoryRegionType::RESERVED};
    g_parsed_mmap_count = 1;
    return;
  }

  uintptr_t mmap_ptr = mbi->mmap_addr;
  const uintptr_t mmap_end = mbi->mmap_addr + mbi->mmap_length;

  while (mmap_ptr < mmap_end && g_parsed_mmap_count < MAX_MEMORY_REGIONS) {
    const auto *entry = reinterpret_cast<multiboot_mmap_entry *>(mmap_ptr);

    auto region_type = kernel::MemoryRegionType::RESERVED;
    if (entry->type == multiboot_mmap_entry_type::MULTIBOOT_MEMORY_AVAILABLE) {
      region_type = kernel::MemoryRegionType::AVAILABLE;
    } else if (entry->type == multiboot_mmap_entry_type::MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) {
      region_type = kernel::MemoryRegionType::ACPI_RECLAIM;
    }

    // Handle marking our own bootloader and module footprints as BOOTLOADER memory
    // so the PMM allocator doesn't overwrite the loaded binaries early on.
    g_parsed_mmap[g_parsed_mmap_count++] = {
        entry->addr,
        entry->len,
        region_type};

    // Advance to the next entry via its size descriptor
    mmap_ptr += entry->size + sizeof(entry->size);
  }
}
}// namespace

// -------------------------------------------------------------------------
// RUNTIME MAIN EXECUTOR
// -------------------------------------------------------------------------

extern "C" void kernel_main(uint32_t mb_physical_addr) {
  init_serial();
  write_fmt("Stage 1.5: Booting with mb_physical_addr = 0x{x}\n\r", mb_physical_addr);

  auto *mbi = reinterpret_cast<multiboot_info_t *>(mb_physical_addr);
  if (!(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count < 2) {
    write_fmt("CRITICAL ERROR: Stage 1.5 requires at least 2 loaded GRUB modules!\n\r");
    while (1);
  }

  auto *mods = reinterpret_cast<multiboot_module_t *>(mbi->mods_addr);
  for (size_t i = 0; i < mbi->mods_count; ++i) {
    auto cmdline = reinterpret_cast<const char *>(mods[i].cmdline);
    write_fmt("Found module: {}\n\r", cmdline);
    write_fmt("Module start: 0x{x}, end: 0x{x}\n\r", mods[i].mod_start, mods[i].mod_end);
    if (kstrstr(cmdline, "hal")) {
      hal_module = &mods[i];
    } else if (kstrstr(cmdline, "kernel")) {
      kernel_module = &mods[i];
    }
  }

  if (!hal_module || !kernel_module) {
    write_fmt("CRITICAL ERROR: Failed to isolate explicit hal/kernel files inside module inputs.\n\r");
    while (1);
  }

  // PASS 1: Read all public exports across binaries into our table space
  collect_symbols_from_module(hal_module);
  collect_symbols_from_module(kernel_module);

  write_fmt("Stage 1.5 Object Registry successfully aggregated {} global symbols.\n\r", g_symbol_count);

  // PASS 2: Physically deploy layout maps to target frames
  load_and_copy_segments(hal_module);
  load_and_copy_segments(kernel_module);
  write_fmt("ELF Executable images expanded into physical frames.\n\r");

  // PASS 3: Patch instructions using link hooks
  process_module_relocations(hal_module);
  process_module_relocations(kernel_module);
  write_fmt("Module symbols cross-stitched and fully relocated.\n\r");

  // -------------------------------------------------------------------------
  // HIGH-HALF INITIALIZATION ROUTINE
  // -------------------------------------------------------------------------

  // 1. Map lower memory space identity lines (0 - 4MB) to preserve the running environment
  for (size_t i = 0; i < 1024; ++i) {
    identity_page_table[i] = (i * 4096) | 0x003;
  }

  // 2. Map HAL Space (0x80000000 -> Base physical frame 8MB)
  for (size_t i = 0; i < 1024; ++i) {
    hal_page_table[i] = (HAL_PHYSICAL_BASE + (i * 4096)) | 0x003;
  }

  // 3. Map Kernel Space (0xC0000000 -> Base physical frame 16MB)
  for (size_t i = 0; i < 1024; ++i) {
    kernel_page_table[i] = (KERNEL_PHYSICAL_BASE + (i * 4096)) | 0x003;
  }

  // 4. Populate Page Directory Slots
  kmemset(boot_page_dir, 0, sizeof(boot_page_dir));
  boot_page_dir[0] = reinterpret_cast<uint32_t>(identity_page_table) | 0x003;
  boot_page_dir[HAL_VIRTUAL_BASE >> 22] = reinterpret_cast<uint32_t>(hal_page_table) | 0x003;
  boot_page_dir[KERNEL_VIRTUAL_BASE >> 22] = reinterpret_cast<uint32_t>(kernel_page_table) | 0x003;

  // 5. Connect the recursive tracking entry loop into slot 1023
  boot_page_dir[1023] = reinterpret_cast<uint32_t>(boot_page_dir) | 0x003;

  write_fmt("Initializing hardware paging architecture tables...\n\r");

  // 6. Push the configurations straight into the processor controls
  asm volatile("mov %0, %%cr3" : : "r"(boot_page_dir));
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000;// Ignite Paging Enable bit flag
  asm volatile("mov %0, %%cr0" : : "r"(cr0));

  write_fmt("Paging engaged. Handing platform execution off to the HAL...\n\r");

  uintptr_t hal_v_end = calculate_elf_virtual_end(hal_module);
  uintptr_t kernel_v_end = calculate_elf_virtual_end(kernel_module);

  // Translate the Multiboot map into your clean, modern structure layout
  parse_multiboot_memory_map(mbi);

  static kernel::boot_info_t boot_info;
  boot_info.page_size = 4096;
  boot_info.hal_more_into_addr = mb_physical_addr;

  boot_info.memory_map = g_parsed_mmap;
  boot_info.memory_map_count = g_parsed_mmap_count;

  // Physical boundaries for the kernel
  boot_info.kernel_physical_start = KERNEL_PHYSICAL_BASE;
  boot_info.kernel_physical_end = KERNEL_PHYSICAL_BASE + (kernel_v_end - KERNEL_VIRTUAL_BASE);

  // Virtual boundaries
  boot_info.kernel_virtual_start = KERNEL_VIRTUAL_BASE;
  boot_info.kernel_virtual_end = kernel_v_end;
  boot_info.hal_virtual_start = HAL_VIRTUAL_BASE;
  boot_info.hal_virtual_end = hal_v_end;

  // Safely forward raw boot command line string args
  boot_info.command_line = (mbi->flags & MULTIBOOT_INFO_CMDLINE)
                               ? reinterpret_cast<const char *>(mbi->cmdline)
                               : "";

  // Extract the HAL entry point function pointer from its ELF header
  auto *hal_ehdr = reinterpret_cast<Elf32_Ehdr *>(hal_module->mod_start);

  // Signature change: Pass the pointer to our boot configuration block
  auto hal_platform_init = reinterpret_cast<void (*)(kernel::boot_info_t *)>(hal_ehdr->e_entry);

  write_fmt("Kernel true virtual boundary end evaluated at: 0x{x}\n\r", boot_info.kernel_virtual_end);
  write_fmt("Jumping to HAL Entry...\n\r");

  hal_platform_init(&boot_info);

  while (1);
}
