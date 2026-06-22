#pragma once

#include <kernel/__core.hpp>

#include <stddef.h>
#include <stdint.h>

namespace hal {
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

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t SHT_REL = 9;
constexpr uint32_t SHF_ALLOC = 0x2;

constexpr uint32_t R_386_32 = 1;
constexpr uint32_t R_386_PC32 = 2;
constexpr uint32_t R_386_GOT32 = 3;
constexpr uint32_t R_386_PLT32 = 4;
constexpr uint32_t R_386_COPY = 5;
constexpr uint32_t R_386_GLOB_DAT = 6;
constexpr uint32_t R_386_JMP_SLOT = 7;
constexpr uint32_t R_386_RELATIVE = 8;
constexpr uint32_t R_386_GOTOFF = 9;
constexpr uint32_t R_386_GOTPC = 10;
constexpr uint32_t R_386_GOT32X = 43;

constexpr uint32_t ELF32_R_SYM(uint32_t val) { return val >> 8; }
constexpr uint32_t ELF32_R_TYPE(uint32_t val) { return val & 0xff; }

}// namespace kernel
