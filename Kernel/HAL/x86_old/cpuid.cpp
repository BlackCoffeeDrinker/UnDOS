
#include "cpuid.hpp"

#include "structs.hpp"

namespace cpuid_bits {
// ECX (CPUID leaf 1)
constexpr uint32_t ECX_SSE3 = 1u << 0;
constexpr uint32_t ECX_PCLMULQDQ = 1u << 1;
constexpr uint32_t ECX_DTES64 = 1u << 2;
constexpr uint32_t ECX_MONITOR = 1u << 3;
constexpr uint32_t ECX_DSCPL = 1u << 4;
constexpr uint32_t ECX_VMX = 1u << 5;
constexpr uint32_t ECX_SMX = 1u << 6;
constexpr uint32_t ECX_EST = 1u << 7;
constexpr uint32_t ECX_TM2 = 1u << 8;
constexpr uint32_t ECX_SSSE3 = 1u << 9;
constexpr uint32_t ECX_CNXT_ID = 1u << 10;
constexpr uint32_t ECX_SDBG = 1u << 11;
constexpr uint32_t ECX_FMA = 1u << 12;
constexpr uint32_t ECX_CMPXCHG16B = 1u << 13;
constexpr uint32_t ECX_XTPR = 1u << 14;
constexpr uint32_t ECX_PDCM = 1u << 15;
constexpr uint32_t ECX_PCID = 1u << 17;
constexpr uint32_t ECX_DCA = 1u << 18;
constexpr uint32_t ECX_SSE4_1 = 1u << 19;
constexpr uint32_t ECX_SSE4_2 = 1u << 20;
constexpr uint32_t ECX_X2APIC = 1u << 21;
constexpr uint32_t ECX_MOVBE = 1u << 22;
constexpr uint32_t ECX_POPCNT = 1u << 23;
constexpr uint32_t ECX_TIMESTAMPCOUNTER_DEADLINE = 1u << 24;
constexpr uint32_t ECX_AES = 1u << 25;
constexpr uint32_t ECX_XSAVE = 1u << 26;
constexpr uint32_t ECX_F16C = 1u << 29;
constexpr uint32_t ECX_RDRAND = 1u << 30;
constexpr uint32_t ECX_HVP = 1u << 31;

// EDX (CPUID leaf 1 legacy)
constexpr uint32_t EDX_FPU = 1u << 0;
constexpr uint32_t EDX_VME = 1u << 1;
constexpr uint32_t EDX_DE = 1u << 2;
constexpr uint32_t EDX_PSE = 1u << 3;
constexpr uint32_t EDX_TIMESTAMPCOUNTER = 1u << 4;
constexpr uint32_t EDX_MSR = 1u << 5;
constexpr uint32_t EDX_PAE = 1u << 6;
constexpr uint32_t EDX_MCE = 1u << 7;
constexpr uint32_t EDX_CMPXCHG8B = 1u << 8;
constexpr uint32_t EDX_APIC = 1u << 9;
constexpr uint32_t EDX_SEP = 1u << 11;
constexpr uint32_t EDX_MTRR = 1u << 12;
constexpr uint32_t EDX_PGE = 1u << 13;
constexpr uint32_t EDX_MCA = 1u << 14;
constexpr uint32_t EDX_CMOV = 1u << 15;
constexpr uint32_t EDX_PAT = 1u << 16;
constexpr uint32_t EDX_PSE36 = 1u << 17;
constexpr uint32_t EDX_PSN = 1u << 18;
constexpr uint32_t EDX_CLFLUSH = 1u << 19;
constexpr uint32_t EDX_DS = 1u << 21;
constexpr uint32_t EDX_ACPI = 1u << 22;
constexpr uint32_t EDX_MMX = 1u << 23;
constexpr uint32_t EDX_FXSR = 1u << 24;
constexpr uint32_t EDX_SSE = 1u << 25;
constexpr uint32_t EDX_SSE2 = 1u << 26;
constexpr uint32_t EDX_SELFSNOOP = 1u << 27;
constexpr uint32_t EDX_HTT = 1u << 28;
constexpr uint32_t EDX_TM = 1u << 29;
constexpr uint32_t EDX_PBE = 1u << 31;
}// namespace cpuid_bits

namespace {
hal::x86::regs cpu_cpuid(int code) {
  hal::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

bool has_cpu_cpuid() {
  // Detect CPU: Check CPUID availability
  bool cpuid_available = false;
  {
    uint32_t old_flags, new_flags;
    asm volatile(
        "pushf\n\t"
        "pop %0\n\t"
        "mov %0, %1\n\t"
        "xor $0x200000, %1\n\t"
        "push %1\n\t"
        "popf\n\t"
        "pushf\n\t"
        "pop %1\n\t"
        : "=&r"(old_flags), "=&r"(new_flags)
        :
        : "cc");
    cpuid_available = ((old_flags ^ new_flags) & 0x200000) != 0;
  }
  return cpuid_available;
}
}// namespace

namespace hal::x86 {
CPUID get_cpuid() {
  CPUID out{};
  out.present = has_cpu_cpuid();
  if (!out.present) {
    return out;
  }

  // Leaf 0 — vendor + max leaf
  const auto info0 = cpu_cpuid(0);
  out.maxCpuIDLeaf = info0.eax;

  // Vendor string: EBX, EDX, ECX (12 bytes)
  auto write_reg = [&](uint32_t reg, int offset) {
    out.vendorID[offset + 0] = static_cast<char>((reg >> 0) & 0xFF);
    out.vendorID[offset + 1] = static_cast<char>((reg >> 8) & 0xFF);
    out.vendorID[offset + 2] = static_cast<char>((reg >> 16) & 0xFF);
    out.vendorID[offset + 3] = static_cast<char>((reg >> 24) & 0xFF);
  };

  write_reg(info0.ebx, 0);
  write_reg(info0.edx, 4);
  write_reg(info0.ecx, 8);
  out.vendorID[12] = '\0';

  // Leaf 1 — feature bits + version info
  const auto info1 = cpu_cpuid(1);

  uint32_t eax = info1.eax;
  uint32_t ebx = info1.ebx;
  uint32_t ecx = info1.ecx;
  uint32_t edx = info1.edx;

  // Version info
  out.stepping = eax & 0xF;
  out.model = (eax >> 4) & 0xF;
  out.family = (eax >> 8) & 0xF;
  out.type = (eax >> 12) & 0x3;
  out.extModel = (eax >> 16) & 0xF;
  out.extFamily = (eax >> 20) & 0xFF;

  // EBX fields
  out.brandIndex = ebx & 0xFF;
  out.clflushLineSize = (ebx >> 8) & 0xFF;
  out.maxLogicalProcessorsPerPackage = (ebx >> 16) & 0xFF;
  out.initialApicId = static_cast<uint8_t>((ebx >> 24) & 0xFF);

  // ECX feature flags
  out.has_SSE3 = ecx & cpuid_bits::ECX_SSE3;
  out.has_PCLMULQDQ = ecx & cpuid_bits::ECX_PCLMULQDQ;
  out.has_DTES64 = ecx & cpuid_bits::ECX_DTES64;
  out.has_MONITOR = ecx & cpuid_bits::ECX_MONITOR;
  out.has_DSCPL = ecx & cpuid_bits::ECX_DSCPL;
  out.has_VMX = ecx & cpuid_bits::ECX_VMX;
  out.has_SMX = ecx & cpuid_bits::ECX_SMX;
  out.has_EST = ecx & cpuid_bits::ECX_EST;
  out.has_TM2 = ecx & cpuid_bits::ECX_TM2;
  out.has_SSSE3 = ecx & cpuid_bits::ECX_SSSE3;
  out.has_CNXT_ID = ecx & cpuid_bits::ECX_CNXT_ID;
  out.has_SDBG = ecx & cpuid_bits::ECX_SDBG;
  out.has_FMA = ecx & cpuid_bits::ECX_FMA;
  out.has_CMPXCHG16B = ecx & cpuid_bits::ECX_CMPXCHG16B;
  out.has_XTPR = ecx & cpuid_bits::ECX_XTPR;
  out.has_PDCM = ecx & cpuid_bits::ECX_PDCM;
  out.has_PCID = ecx & cpuid_bits::ECX_PCID;
  out.has_DCA = ecx & cpuid_bits::ECX_DCA;
  out.has_SSE4_1 = ecx & cpuid_bits::ECX_SSE4_1;
  out.has_SSE4_2 = ecx & cpuid_bits::ECX_SSE4_2;
  out.has_X2APIC = ecx & cpuid_bits::ECX_X2APIC;
  out.has_MOVBE = ecx & cpuid_bits::ECX_MOVBE;
  out.has_POPCNT = ecx & cpuid_bits::ECX_POPCNT;
  out.has_TSC_DL = ecx & cpuid_bits::ECX_TIMESTAMPCOUNTER_DEADLINE;
  out.has_AES = ecx & cpuid_bits::ECX_AES;
  out.has_XSAVE = ecx & cpuid_bits::ECX_XSAVE;
  out.has_F16C = ecx & cpuid_bits::ECX_F16C;
  out.has_RDRAND = ecx & cpuid_bits::ECX_RDRAND;
  out.has_HVP = ecx & cpuid_bits::ECX_HVP;

  // EDX legacy feature flags
  out.has_FPU = edx & cpuid_bits::EDX_FPU;
  out.has_VME = edx & cpuid_bits::EDX_VME;
  out.has_DE = edx & cpuid_bits::EDX_DE;
  out.has_PSE = edx & cpuid_bits::EDX_PSE;
  out.has_TSC = edx & cpuid_bits::EDX_TIMESTAMPCOUNTER;
  out.has_MSR = edx & cpuid_bits::EDX_MSR;
  out.has_PAE = edx & cpuid_bits::EDX_PAE;
  out.has_MCE = edx & cpuid_bits::EDX_MCE;
  out.has_CMPXCHG8B = edx & cpuid_bits::EDX_CMPXCHG8B;
  out.has_APIC = edx & cpuid_bits::EDX_APIC;
  out.has_SEP = edx & cpuid_bits::EDX_SEP;
  out.has_MTRR = edx & cpuid_bits::EDX_MTRR;
  out.has_PGE = edx & cpuid_bits::EDX_PGE;
  out.has_MCA = edx & cpuid_bits::EDX_MCA;
  out.has_CMOV = edx & cpuid_bits::EDX_CMOV;
  out.has_PAT = edx & cpuid_bits::EDX_PAT;
  out.has_PSE36 = edx & cpuid_bits::EDX_PSE36;
  out.has_PSN = edx & cpuid_bits::EDX_PSN;
  out.has_CLFLUSH = edx & cpuid_bits::EDX_CLFLUSH;
  out.has_DS = edx & cpuid_bits::EDX_DS;
  out.has_ACPI = edx & cpuid_bits::EDX_ACPI;
  out.has_MMX = edx & cpuid_bits::EDX_MMX;
  out.has_FXSR = edx & cpuid_bits::EDX_FXSR;
  out.has_SSE = edx & cpuid_bits::EDX_SSE;
  out.has_SSE2 = edx & cpuid_bits::EDX_SSE2;
  out.has_SELFSNOOP = edx & cpuid_bits::EDX_SELFSNOOP;
  out.has_HTT = edx & cpuid_bits::EDX_HTT;
  out.has_TM = edx & cpuid_bits::EDX_TM;
  out.has_PBE = edx & cpuid_bits::EDX_PBE;

  return out;
}

}// namespace hal::x86
