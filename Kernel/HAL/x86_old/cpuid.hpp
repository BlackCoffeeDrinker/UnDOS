
#pragma once

#include <array.hpp>

namespace hal::x86 {
struct CPUID {
  bool present = false;

  uint32_t maxCpuIDLeaf = 0;
  kstd::array<char, 13> vendorID;

  uint8_t stepping = 0;
  uint8_t model = 0;
  uint8_t family = 0;
  uint8_t type = 0;
  uint8_t extModel = 0;
  uint8_t extFamily = 0;

  uint8_t brandIndex = 0;
  uint8_t clflushLineSize = 0;
  uint8_t maxLogicalProcessorsPerPackage = 0;
  uint8_t initialApicId = 0;

  bool has_SSE3;
  bool has_PCLMULQDQ;
  bool has_DTES64;
  bool has_MONITOR;
  bool has_DSCPL;
  bool has_VMX;
  bool has_SMX;
  bool has_EST;
  bool has_TM2;
  bool has_SSSE3;
  bool has_CNXT_ID;
  bool has_SDBG;
  bool has_FMA;
  bool has_CMPXCHG16B;
  bool has_XTPR;
  bool has_PDCM;
  bool has_PCID;
  bool has_DCA;
  bool has_SSE4_1;
  bool has_SSE4_2;
  bool has_X2APIC;
  bool has_MOVBE;
  bool has_POPCNT;
  bool has_TSC_DL;
  bool has_AES;
  bool has_XSAVE;
  bool has_F16C;
  bool has_RDRAND;
  bool has_HVP;

  bool has_FPU;
  bool has_VME;
  bool has_DE;
  bool has_PSE;
  bool has_TSC;
  bool has_MSR;
  bool has_PAE;
  bool has_MCE;
  bool has_CMPXCHG8B;
  bool has_APIC;
  bool has_SEP;
  bool has_MTRR;
  bool has_PGE;
  bool has_MCA;
  bool has_CMOV;
  bool has_PAT;
  bool has_PSE36;
  bool has_PSN;
  bool has_CLFLUSH;
  bool has_DS;
  bool has_ACPI;
  bool has_MMX;
  bool has_FXSR;
  bool has_SSE;
  bool has_SSE2;
  bool has_SELFSNOOP;
  bool has_HTT;
  bool has_TM;
  bool has_PBE;
};

CPUID get_cpuid();

}// namespace hal::x86
