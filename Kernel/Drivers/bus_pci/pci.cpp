

#include <Kernel.hpp>
#include <kernel/pci.hpp>
#include <kernel/resource.hpp>
#include <kernel/spinlock.hpp>

// PCI bus driver for 486-class through Pentium 4 machines.
//
// Host bridge access uses Configuration Mechanism #1 (0xCF8/0xCFC) or the
// legacy Mechanism #2 (0xCF8/0xCFA + 0xC000-0xCFFF window). Detection only
// touches 0xCF8/0xCFA and validates bus 0 before trusting either mechanism,
// so a machine without PCI just logs one line and the bus has no children.

namespace {

using kernel::pci::Bar;
using kernel::pci::DeviceInfo;
using kernel::pci::IrqSource;

// region Config space constants
constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;// #1 address / #2 CSE
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;   // #1 data
constexpr uint16_t PCI_CONFIG_FORWARD = 0xCFA;// #2 forward (bus) register
constexpr uint16_t PCI_TYPE2_WINDOW = 0xC000; // #2 config window

constexpr uint8_t REG_VENDOR = 0x00;
constexpr uint8_t REG_DEVICE = 0x02;
constexpr uint8_t REG_COMMAND = 0x04;
constexpr uint8_t REG_STATUS = 0x06;
constexpr uint8_t REG_REVISION = 0x08;
constexpr uint8_t REG_PROGIF = 0x09;
constexpr uint8_t REG_SUBCLASS = 0x0A;
constexpr uint8_t REG_CLASS = 0x0B;
constexpr uint8_t REG_HEADER_TYPE = 0x0E;
constexpr uint8_t REG_BAR0 = 0x10;
constexpr uint8_t REG_SECONDARY_BUS = 0x19;// header type 1
constexpr uint8_t REG_SUBSYS_VENDOR = 0x2C;// header type 0
constexpr uint8_t REG_SUBSYS_ID = 0x2E;    // header type 0
constexpr uint8_t REG_CAP_PTR = 0x34;
constexpr uint8_t REG_INT_LINE = 0x3C;
constexpr uint8_t REG_INT_PIN = 0x3D;

constexpr uint16_t CMD_IO = 0x0001;
constexpr uint16_t CMD_MEMORY = 0x0002;
constexpr uint16_t STATUS_CAP_LIST = 0x0010;
constexpr uint8_t HEADER_MULTIFUNCTION = 0x80;
constexpr uint8_t CAP_ID_MSI = 0x05;

constexpr uint8_t CLASS_BRIDGE = 0x06;
constexpr uint8_t SUBCLASS_PCI_PCI = 0x04;
// endregion

enum class Mechanism : uint8_t { None,
                                 Type1,
                                 Type2 };

struct PciAddress {
  uint8_t bus, dev, fn;
};

Mechanism g_mechanism = Mechanism::None;
kernel::SpinLock g_configLock;

// region Configuration access
// Raw dword read; caller holds g_configLock.
uint32_t RawRead32(const PciAddress a, const uint8_t off) {
  if (g_mechanism == Mechanism::Type1) {
    HAL_IO_Out32(PCI_CONFIG_ADDRESS, 0x80000000u 
      | (static_cast<uint32_t>(a.bus) << 16) 
      | (static_cast<uint32_t>(a.dev & 0x1F) << 11) 
      | (static_cast<uint32_t>(a.fn & 7) << 8)
      | (off & 0xFC));
    
    return HAL_IO_In32(PCI_CONFIG_DATA);
  }
  if (g_mechanism == Mechanism::Type2) {
    if (a.dev >= 16)
      return 0xFFFFFFFF;
    HAL_IO_Out8(PCI_CONFIG_ADDRESS, static_cast<uint8_t>(0xF0 | ((a.fn & 7) << 1)));
    HAL_IO_Out8(PCI_CONFIG_FORWARD, a.bus);
    const uint32_t v = HAL_IO_In32(static_cast<uint16_t>(PCI_TYPE2_WINDOW | (a.dev << 8) | (off & 0xFC)));
    HAL_IO_Out8(PCI_CONFIG_ADDRESS, 0);
    return v;
  }
  return 0xFFFFFFFF;
}

// Raw access of width 1/2/4 at a naturally aligned offset; caller holds the lock.
void RawWrite(const PciAddress a, const uint8_t off, const uint8_t width, const uint32_t v) {
  uint16_t port;
  if (g_mechanism == Mechanism::Type1) {
    HAL_IO_Out32(PCI_CONFIG_ADDRESS, 0x80000000u
      | (static_cast<uint32_t>(a.bus) << 16)
      | (static_cast<uint32_t>(a.dev & 0x1F) << 11) 
      | (static_cast<uint32_t>(a.fn & 7) << 8) 
      | (off & 0xFC));
    
    port = static_cast<uint16_t>(PCI_CONFIG_DATA + (off & 3));
  } else if (g_mechanism == Mechanism::Type2) {
    if (a.dev >= 16)
      return;
    HAL_IO_Out8(PCI_CONFIG_ADDRESS, static_cast<uint8_t>(0xF0 | ((a.fn & 7) << 1)));
    HAL_IO_Out8(PCI_CONFIG_FORWARD, a.bus);
    port = static_cast<uint16_t>(PCI_TYPE2_WINDOW | (a.dev << 8) | off);
  } else {
    return;
  }
  
  switch (width) {
    case 1: HAL_IO_Out8(port, static_cast<uint8_t>(v)); break;
    case 2: HAL_IO_Out16(port, static_cast<uint16_t>(v)); break;
    default: HAL_IO_Out32(port, v); break;
  }
  
  if (g_mechanism == Mechanism::Type2)
    HAL_IO_Out8(PCI_CONFIG_ADDRESS, 0);
}

uint32_t RawRead(const PciAddress a, const uint8_t off, const uint8_t width) {
  const uint32_t d = RawRead32(a, off);
  const uint32_t shifted = d >> ((off & 3) * 8);
  if (width == 1)
    return shifted & 0xFF;
  if (width == 2)
    return shifted & 0xFFFF;
  return d;
}

uint16_t Read16(const PciAddress a, const uint8_t off) {
  kernel::IrqSpinLockGuard guard(g_configLock);
  return static_cast<uint16_t>(RawRead(a, off, 2));
}
uint8_t Read8(const PciAddress a, const uint8_t off) {
  kernel::IrqSpinLockGuard guard(g_configLock);
  return static_cast<uint8_t>(RawRead(a, off, 1));
}

bool ValidWidth(const uint8_t off, const uint8_t width) {
  return (width == 1 || width == 2 || width == 4) && (off % width) == 0;
}
// endregion

// region Detection
bool BusZeroHasDevice() {
  const uint8_t maxDev = g_mechanism == Mechanism::Type2 ? 16 : 32;
  for (uint8_t dev = 0; dev < maxDev; ++dev) {
    const uint16_t vendor = Read16({.bus = 0, .dev = dev, .fn = 0}, REG_VENDOR);
    if (vendor != 0xFFFF && vendor != 0x0000)
      return true;
  }
  return false;
}

Mechanism DetectMechanism() {
  {
    // Mechanism #1: the address register latches bit 31 and reads it back
    // (same sequence as Linux pci_check_type1). The saved value is written
    // back only when the register proved to be a 32-bit latch: on ISA-only
    // and Mechanism #2 machines the read returns 0xFFFFFFFF, and writing that
    // back would enable the #2 CSE window and hit the 0xCF9 reset register.
    kernel::IrqSpinLockGuard guard(g_configLock);
    HAL_IO_Out8(PCI_CONFIG_ADDRESS + 3, 0x01);
    const uint32_t saved = HAL_IO_In32(PCI_CONFIG_ADDRESS);
    HAL_IO_Out32(PCI_CONFIG_ADDRESS, 0x80000000u);
    const uint32_t readBack = HAL_IO_In32(PCI_CONFIG_ADDRESS);
    if (readBack == 0x80000000u) {
      g_mechanism = Mechanism::Type1;
      if (saved != 0xFFFFFFFFu)
        HAL_IO_Out32(PCI_CONFIG_ADDRESS, saved & ~0x80000000u);
    } else {
      // Leave a Mechanism #2 CSE register disabled.
      HAL_IO_Out8(PCI_CONFIG_ADDRESS, 0x00);
    }
  }
  
  if (g_mechanism == Mechanism::Type1) {
    if (BusZeroHasDevice())
      return Mechanism::Type1;
    g_mechanism = Mechanism::None;
  }

  {
    // Mechanism #2: CSE and forward registers are 8-bit read/write.
    kernel::IrqSpinLockGuard guard(g_configLock);
    HAL_IO_Out8(PCI_CONFIG_ADDRESS, 0x00);
    HAL_IO_Out8(PCI_CONFIG_FORWARD, 0x00);
    if (HAL_IO_In8(PCI_CONFIG_ADDRESS) == 0x00 && HAL_IO_In8(PCI_CONFIG_FORWARD) == 0x00)
      g_mechanism = Mechanism::Type2;
  }
  
  if (g_mechanism == Mechanism::Type2) {
    if (BusZeroHasDevice())
      return Mechanism::Type2;
    g_mechanism = Mechanism::None;
  }
  return Mechanism::None;
}
// endregion

// region $PIR routing table
struct PirLink {
  uint8_t link;
  uint16_t bitmap;
};

struct PirEntry {
  uint8_t bus, dev;
  PirLink pins[4];
};

constexpr size_t kMaxPirEntries = 64;
PirEntry g_pir[kMaxPirEntries];
size_t g_pirCount = 0;
bool g_pirPresent = false;

void LoadPir() {
  constexpr uint32_t kBiosBase = 0xF0000;
  constexpr size_t kBiosSize = 0x10000;
  constexpr size_t PAGE_SIZE = 4096;

  auto &as = KE_VMM_GetKernelAddressSpace();
  const auto region = KE_VMM_AllocateRegion(as, kBiosSize, kernel::vmm::ProtectFlags::READ);
  if (!region)
    return;
  const kernel::VirtualAddress base = kernel::VirtualAddress::from_ptr(*region);
  for (size_t i = 0; i < kBiosSize / PAGE_SIZE; ++i) {
    if (KE_VMM_MapPhysical(as, base + (i * PAGE_SIZE), kernel::PhysicalAddress(kBiosBase + (i * PAGE_SIZE)), kernel::vmm::ProtectFlags::READ)) {
      for (size_t j = 0; j < i; ++j) {
        HAL_VMM_UnmapPage(base + (j * PAGE_SIZE));
        HAL_VMM_Flush(base + (j * PAGE_SIZE));
      }
      (void) KE_VMM_FreeRegion(as, base.as_ptr());
      return;
    }
  }

  const auto *rom = static_cast<const uint8_t *>(base.as_ptr());
  for (size_t off = 0; off + 32 <= kBiosSize; off += 16) {
    if (rom[off] != '$' || rom[off + 1] != 'P' || rom[off + 2] != 'I' || rom[off + 3] != 'R')
      continue;
    const uint16_t version = static_cast<uint16_t>(rom[off + 4] | (rom[off + 5] << 8));
    const uint16_t size = static_cast<uint16_t>(rom[off + 6] | (rom[off + 7] << 8));
    if (version != 0x0100 || size < 32 || (size % 16) != 0 || off + size > kBiosSize)
      continue;
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum = static_cast<uint8_t>(sum + rom[off + i]);
    if (sum != 0)
      continue;

    g_pirPresent = true;
    for (size_t e = off + 32; e + 16 <= off + size && g_pirCount < kMaxPirEntries; e += 16) {
      auto &entry = g_pir[g_pirCount++];
      entry.bus = rom[e];
      entry.dev = static_cast<uint8_t>(rom[e + 1] >> 3);
      for (size_t p = 0; p < 4; ++p) {
        entry.pins[p].link = rom[e + 2 + (p * 3)];
        entry.pins[p].bitmap = static_cast<uint16_t>(rom[e + 3 + (p * 3)] | (rom[e + 4 + (p * 3)] << 8));
      }
    }
    break;
  }

  // KE_VMM_FreeRegion only releases the reservation; unmap the pages first.
  for (size_t j = 0; j < kBiosSize / PAGE_SIZE; ++j) {
    HAL_VMM_UnmapPage(base + (j * PAGE_SIZE));
    HAL_VMM_Flush(base + (j * PAGE_SIZE));
  }
  (void) KE_VMM_FreeRegion(as, base.as_ptr());
}

const PirLink *FindPirLink(const uint8_t bus, const uint8_t dev, const uint8_t pin) {
  for (size_t i = 0; i < g_pirCount; ++i) {
    if (g_pir[i].bus == bus && g_pir[i].dev == dev) {
      const auto &l = g_pir[i].pins[pin - 1];
      return l.link ? &l : nullptr;
    }
  }
  return nullptr;
}
// endregion

// region Enumeration state
struct BusParent {
  bool valid = false;
  uint8_t bus = 0, dev = 0;// the bridge leading to this bus
};
BusParent g_busParent[256];
uint32_t g_visited[256 / 32];

constexpr size_t kMaxChildren = 128;

struct PciBusContext {
  kstd::array<kernel::KDevicePtr<kernel::KDevice>, kMaxChildren> children;
  size_t childCount = 0;
};

struct PciChildContext {
  DeviceInfo info;
  // '\n'-separated, most specific first (see BuildHardwareIds).
  kstd::static_string<192> hardwareId;
};

void ResolveIrq(DeviceInfo &info) {
  info.irq = -1;
  info.irqSource = IrqSource::None;
  if (info.intPin == 0 || info.intPin > 4)
    return;

  const uint8_t line = Read8({.bus = info.bus, .dev = info.dev, .fn = info.fn}, REG_INT_LINE);
  if (line == 0 || line > 15)
    return;

  // Swizzle the pin up through bridges until a $PIR entry matches.
  uint8_t bus = info.bus, dev = info.dev, pin = info.intPin;
  const PirLink *link = nullptr;
  for (int hop = 0; hop < 256 && g_pirPresent; ++hop) {
    link = FindPirLink(bus, dev, pin);
    if (link || !g_busParent[bus].valid)
      break;
    pin = static_cast<uint8_t>((((pin - 1) + dev) % 4) + 1);
    const auto parent = g_busParent[bus];
    bus = parent.bus;
    dev = parent.dev;
  }

  info.irq = line;
  info.irqSource = (link && (link->bitmap & (1u << line))) ? IrqSource::Pir : IrqSource::InterruptLine;
}

void SizeBars(DeviceInfo &info) {
  // Host bridges are never sized: on several 486/Pentium chipsets turning
  // off their memory decode (or moving a BAR) stops DRAM decode and hangs
  // the CPU (Linux quirk_mmio_always_on).
  if (info.barCount == 0 || (info.classCode == 0x06 && info.subClass == 0x00))
    return;

  const PciAddress a{.bus = info.bus, .dev = info.dev, .fn = info.fn};
  kernel::IrqSpinLockGuard guard(g_configLock);

  const auto command = static_cast<uint16_t>(RawRead(a, REG_COMMAND, 2));

  for (uint8_t i = 0; i < info.barCount; ++i) {
    const auto reg = static_cast<uint8_t>(REG_BAR0 + (i * 4));
    const uint32_t original = RawRead32(a, reg);
    // Only the decode bit matching this BAR's type is turned off while sizing.
    const uint16_t decode = (original & 1) ? CMD_IO : CMD_MEMORY;
    if (command & decode)
      RawWrite(a, REG_COMMAND, 2, static_cast<uint16_t>(command & ~decode));
    RawWrite(a, reg, 4, 0xFFFFFFFF);
    const uint32_t probe = RawRead32(a, reg);
    RawWrite(a, reg, 4, original);
    if (command & decode)
      RawWrite(a, REG_COMMAND, 2, command);
    if (probe == 0 || probe == 0xFFFFFFFF)
      continue;

    Bar &bar = info.bars[i];
    if (original & 1) {
      const uint32_t mask = probe & 0xFFFFFFFC & 0xFFFF;
      bar.io = true;
      bar.base = original & 0xFFFFFFFC;
      bar.size = mask ? ((~mask & 0xFFFF) + 1) : 0;
    } else {
      bar.prefetch = (original & 0x08) != 0;
      bar.base = original & 0xFFFFFFF0;
      bar.size = ~(probe & 0xFFFFFFF0) + 1;
      if (((original >> 1) & 3) == 2 && i + 1 < info.barCount) {
        bar.is64 = true;
        const auto hiReg = static_cast<uint8_t>(reg + 4);
        const uint32_t hi = RawRead32(a, hiReg);
        ++i;// the high dword occupies the next slot
        if (hi != 0) {
          // Above 4 GiB: unreachable on i386, record as unusable.
          bar.base = 0;
          bar.size = 0;
        }
      }
    }
  }
}

void WalkCapabilities(DeviceInfo &info) {
  const PciAddress a{.bus = info.bus, .dev = info.dev, .fn = info.fn};
  if (!(Read16(a, REG_STATUS) & STATUS_CAP_LIST))
    return;
  uint8_t ptr = Read8(a, REG_CAP_PTR) & 0xFC;
  for (int n = 0; n < 48 && ptr >= 0x40; ++n) {
    const uint8_t id = Read8(a, ptr);
    if (id == CAP_ID_MSI && info.msiCapOffset == 0) {
      const uint16_t control = Read16(a, static_cast<uint8_t>(ptr + 2));
      info.msiCapOffset = ptr;
      info.msi64 = (control & 0x0080) != 0;
      info.msiPerVectorMask = (control & 0x0100) != 0;
    }
    ptr = Read8(a, static_cast<uint8_t>(ptr + 1)) & 0xFC;
  }
}

void LogDevice(const DeviceInfo &d) {
  static constexpr char kPins[] = {'-', 'A', 'B', 'C', 'D'};
  KE_Print("PCI: {x:02}:{x:02}.{} {x:04}:{x:04} class {x:02}/{x:02}/{x:02} hdr {x:02} IRQ pin {} line {}{}",
           d.bus, d.dev, d.fn, d.vendorId, d.deviceId, d.classCode, d.subClass, d.progIf, d.headerType,
           kPins[d.intPin <= 4 ? d.intPin : 0], d.irq,
           d.msiCapOffset ? " [MSI]" : "");
  for (uint8_t i = 0; i < d.barCount; ++i) {
    const Bar &b = d.bars[i];
    if (!b.size)
      continue;
    KE_Print("PCI:   BAR{} {} 0x{x:08} size 0x{x}{}{}", i, b.io ? "io" : "mem", b.base, b.size,
             b.prefetch ? " pf" : "", b.is64 ? " 64" : "");
  }
}

// Windows-style IDs with uppercase, zero-padded hex (kstd::format's {x:04}
// neither pads nor uppercases), most specific first:
//   PCI\VEN_v&DEV_d&SUBSYS_ssssvvvv&REV_rr, PCI\VEN_v&DEV_d&SUBSYS_ssssvvvv,
//   PCI\VEN_v&DEV_d&REV_rr, PCI\VEN_v&DEV_d, PCI\CC_ccss, PCI\CC_ccsspp.
void BuildHardwareIds(const DeviceInfo &info, kstd::static_string<192> &out) {
  constexpr char kHex[] = "0123456789ABCDEF";
  char buf[192];
  size_t n = 0;
  const auto put = [&](const char *s) {
    while (*s && n < sizeof(buf)) buf[n++] = *s++;
  };
  const auto hex = [&](const uint32_t v, const int digits) {
    for (int i = digits - 1; i >= 0 && n < sizeof(buf); --i) buf[n++] = kHex[(v >> (4 * i)) & 0xF];
  };
  const auto venDev = [&] {
    put("PCI\\VEN_");
    hex(info.vendorId, 4);
    put("&DEV_");
    hex(info.deviceId, 4);
  };
  const uint32_t subsys = (static_cast<uint32_t>(info.subDeviceId) << 16) | info.subVendorId;
  if (subsys) {
    venDev();
    put("&SUBSYS_");
    hex(subsys, 8);
    put("&REV_");
    hex(info.revision, 2);
    put("\n");
    venDev();
    put("&SUBSYS_");
    hex(subsys, 8);
    put("\n");
  }
  venDev();
  put("&REV_");
  hex(info.revision, 2);
  put("\n");
  venDev();
  put("\nPCI\\CC_");
  hex(info.classCode, 2);
  hex(info.subClass, 2);
  hex(info.progIf, 2);
  put("\nPCI\\CC_");
  hex(info.classCode, 2);
  hex(info.subClass, 2);
  kstd::format(out, "{}", kstd::string_view(buf, n));
}

bool IsPublished(const PciBusContext *ctx, const PciAddress a) {
  for (size_t i = 0; i < ctx->childCount; ++i) {
    const auto *existing = ctx->children[i]->deviceExtension.as<PciChildContext>();
    if (existing && existing->info.bus == a.bus && existing->info.dev == a.dev && existing->info.fn == a.fn)
      return true;
  }
  return false;
}

// Buses still to scan; bridges push their secondary bus here instead of
// recursing, so deep topologies use no kernel stack.
struct BusWorkList {
  uint8_t buses[256];
  size_t count = 0;
  void push(const uint8_t bus) {
    if (g_visited[bus / 32] & (1u << (bus % 32)))
      return;
    g_visited[bus / 32] |= 1u << (bus % 32);
    buses[count++] = bus;// at most 256 distinct buses
  }
};

void ScanFunction(const kernel::KDevicePtr<kernel::KDevice> &busPdo, PciBusContext *ctx, const PciAddress a, BusWorkList &work) {
  const uint8_t headerType = Read8(a, REG_HEADER_TYPE);
  const uint8_t layout = headerType & 0x7F;

  // A rescan keeps the PDO published earlier for the same function, and
  // never re-sizes its BARs: sizing briefly turns off decode on a device a
  // bound driver may be using. Bridges are still followed for new buses.
  if (!IsPublished(ctx, a)) {
    DeviceInfo info{};
    info.bus = a.bus;
    info.dev = a.dev;
    info.fn = a.fn;
    info.vendorId = Read16(a, REG_VENDOR);
    info.deviceId = Read16(a, REG_DEVICE);
    info.revision = Read8(a, REG_REVISION);
    info.progIf = Read8(a, REG_PROGIF);
    info.subClass = Read8(a, REG_SUBCLASS);
    info.classCode = Read8(a, REG_CLASS);
    info.headerType = headerType;
    info.barCount = layout == 0 ? 6 : (layout == 1 ? 2 : 0);
    if (layout == 0) {
      info.subVendorId = Read16(a, REG_SUBSYS_VENDOR);
      info.subDeviceId = Read16(a, REG_SUBSYS_ID);
    }
    if (layout <= 1)
      info.intPin = Read8(a, REG_INT_PIN);

    SizeBars(info);
    if (layout <= 1)
      WalkCapabilities(info);
    ResolveIrq(info);
    LogDevice(info);

    if (ctx->childCount < ctx->children.size()) {
      if (const auto child = KE_IO_CreateDevice(busPdo->driverObject, sizeof(PciChildContext), "", kernel::device_type::Unknown)) {
        auto *childCtx = (*child)->deviceExtension.as<PciChildContext>();
        childCtx->info = info;
        BuildHardwareIds(info, childCtx->hardwareId);
        ctx->children[ctx->childCount++] = *child;
      }
    } else {
      KE_Print("PCI: child table full, {x:02}:{x:02}.{} not published", a.bus, a.dev, a.fn);
    }
  }

  if (layout == 1 && Read8(a, REG_CLASS) == CLASS_BRIDGE && Read8(a, REG_SUBCLASS) == SUBCLASS_PCI_PCI) {
    const uint8_t secondary = Read8(a, REG_SECONDARY_BUS);
    if (secondary != 0 && secondary != a.bus && !g_busParent[secondary].valid) {
      g_busParent[secondary] = {.valid = true, .bus = a.bus, .dev = a.dev};
    }
    if (secondary != 0)
      work.push(secondary);
  }
}

void ScanBus(const kernel::KDevicePtr<kernel::KDevice> &busPdo, PciBusContext *ctx, const uint8_t bus, BusWorkList &work) {
  const uint8_t maxDev = g_mechanism == Mechanism::Type2 ? 16 : 32;
  for (uint8_t dev = 0; dev < maxDev; ++dev) {
    const uint16_t vendor = Read16({.bus = bus, .dev = dev, .fn = 0}, REG_VENDOR);
    if (vendor == 0xFFFF || vendor == 0x0000)
      continue;
    ScanFunction(busPdo, ctx, {.bus = bus, .dev = dev, .fn = 0}, work);
    if (Read8({.bus = bus, .dev = dev, .fn = 0}, REG_HEADER_TYPE) & HEADER_MULTIFUNCTION) {
      for (uint8_t fn = 1; fn < 8; ++fn) {
        const uint16_t v = Read16({.bus = bus, .dev = dev, .fn = fn}, REG_VENDOR);
        if (v == 0xFFFF || v == 0x0000)
          continue;
        ScanFunction(busPdo, ctx, {.bus = bus, .dev = dev, .fn = fn}, work);
      }
    }
  }
}
// endregion

// region Driver callbacks
void Pci_EnumerateDevices(const kernel::KDevicePtr<kernel::KDevice> &busPdo) {
  auto *ctx = busPdo->deviceExtension.as<PciBusContext>();
  if (!ctx)
    return;

  // Detection and $PIR loading run once: a rescan must not reset the
  // mechanism while bound drivers use ConfigRead/ConfigWrite, and bridge
  // parents stay valid for IRQ routing of published functions.
  if (g_mechanism == Mechanism::None) {
    const Mechanism mech = DetectMechanism();
    if (mech == Mechanism::None) {
      KE_Print("PCI: no host bridge, bus not present");
      return;
    }
    {
      kernel::IrqSpinLockGuard guard(g_configLock);
      g_mechanism = mech;
    }
    g_pirCount = 0;
    g_pirPresent = false;
    LoadPir();
  }
  const Mechanism mech = g_mechanism;

  for (auto &v: g_visited) v = 0;

  BusWorkList work;
  work.push(0);
  // A multi-function host bridge at 00:00 means fn N hosts root bus N.
  if (Read8({.bus = 0, .dev = 0, .fn = 0}, REG_HEADER_TYPE) & HEADER_MULTIFUNCTION) {
    for (uint8_t fn = 1; fn < 8; ++fn) {
      const uint16_t v = Read16({.bus = 0, .dev = 0, .fn = fn}, REG_VENDOR);
      if (v == 0xFFFF || v == 0x0000)
        continue;
      if (Read8({.bus = 0, .dev = 0, .fn = fn}, REG_CLASS) == CLASS_BRIDGE && Read8({.bus = 0, .dev = 0, .fn = fn}, REG_SUBCLASS) == 0x00)
        work.push(fn);
    }
  }
  for (size_t i = 0; i < work.count; ++i)
    ScanBus(busPdo, ctx, work.buses[i], work);

  KE_Print("PCI: mechanism #{}, {} functions, $PIR {}", mech == Mechanism::Type1 ? 1 : 2, ctx->childCount,
           g_pirPresent ? "present" : "absent");
}

size_t Pci_GetChildCount(const kernel::KDevicePtr<kernel::KDevice> &busPdo) {
  const auto *ctx = busPdo->deviceExtension.as<PciBusContext>();
  return ctx ? ctx->childCount : 0;
}

kernel::KDevicePtr<kernel::KDevice> Pci_GetChild(const kernel::KDevicePtr<kernel::KDevice> &busPdo, size_t index) {
  if (auto *ctx = busPdo->deviceExtension.as<PciBusContext>(); ctx && index < ctx->childCount) {
    return ctx->children[index];
  }
  return nullptr;
}

kstd::string_view Pci_GetHardwareId(const kernel::KDevicePtr<kernel::KDevice> &childPdo) {
  if (const auto *ctx = childPdo->deviceExtension.as<PciChildContext>())
    return ctx->hardwareId;
  return "UNKNOWN_PCI_DEVICE";
}

kstd::expected<kernel::IoResourceRequirementsList *, kstd::error_code> Pci_QueryResourceRequirements(const kernel::KDevicePtr<kernel::KDevice> &childPdo) {
  const auto *ctx = childPdo->deviceExtension.as<PciChildContext>();
  if (!ctx)
    return static_cast<kernel::IoResourceRequirementsList *>(nullptr);
  const DeviceInfo &d = ctx->info;

  // VGA-compatible display controllers (class 03/00) also decode the legacy
  // VGA window and ports, which are not BARs (Windows 2000 does the same).
  // They go first so a driver that takes the first memory resource as its
  // banked window still finds 0xA0000. Exclusive, so an ISA/legacy
  // reservation of the same card conflicts instead of binding it twice.
  const bool legacyVga = d.classCode == 0x03 && d.subClass == 0x00;
  bool any = d.irq >= 0 || legacyVga;
  for (uint8_t i = 0; i < d.barCount; ++i) any = any || (d.bars[i].size && d.bars[i].base);
  if (!any)
    return static_cast<kernel::IoResourceRequirementsList *>(nullptr);

  auto req = KE_RES_CreateRequirementsList(kernel::device_type::Unknown);
  if (!req)
    return req;
  const auto alt = KE_RES_AddAlternative(*req);
  if (!alt) {
    KE_RES_FreeRequirementsList(*req);
    return kstd::unexpected(alt.error());
  }

  kstd::error_code status = kernel::KernelError::Success;
  if (legacyVga) {
    KE_Print("bus_pci: {}:{}.{} VGA class: adding legacy 0xA0000-0xBFFFF and ports 0x3B0-0x3DF", d.bus, d.dev, d.fn);
    status = KE_RES_AddMemory(*alt, 0xA0000, 0x20000, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
    if (!status)
      status = KE_RES_AddPort(*alt, 0x3B0, 0x30, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  for (uint8_t i = 0; i < d.barCount && !status; ++i) {
    const Bar &b = d.bars[i];
    if (!b.size || !b.base)
      continue;
    status = b.io ? KE_RES_AddPort(*alt, b.base, b.size, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive)
                  : KE_RES_AddMemory(*alt, b.base, b.size, kernel::ResourceOption::Fixed, kernel::ResourceShare::DeviceExclusive);
  }
  if (!status && d.irq >= 0)
    status = KE_RES_AddInterrupt(*alt, static_cast<uint32_t>(d.irq), kernel::ResourceOption::Fixed, kernel::ResourceShare::Shared);
  if (status) {
    KE_RES_FreeRequirementsList(*req);
    return kstd::unexpected(status);
  }
  return *req;
}

kstd::error_code Pci_DeviceControl(const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t code,
                                   kstd::span<const uint8_t> in, kstd::span<uint8_t> out, size_t &transferred) {
  transferred = 0;
  const auto *ctx = device->deviceExtension.as<PciChildContext>();
  if (!ctx)
    return kernel::KernelError::Unsupported;
  const DeviceInfo &d = ctx->info;
  const PciAddress a{.bus = d.bus, .dev = d.dev, .fn = d.fn};

  switch (code) {
    case kernel::pci::IOCTL_PCI_GET_DEVICE_INFO:
      if (out.size() < sizeof(DeviceInfo))
        return kernel::KernelError::InvalidArgument;
      __builtin_memcpy(out.data(), &d, sizeof(DeviceInfo));
      transferred = sizeof(DeviceInfo);
      return kernel::KernelError::Success;

    case kernel::pci::IOCTL_PCI_CONFIG_READ: {
      kernel::pci::ConfigRequest r{};
      if (in.size() < sizeof(r) || out.size() < sizeof(uint32_t))
        return kernel::KernelError::InvalidArgument;
      __builtin_memcpy(&r, in.data(), sizeof(r));
      if (!ValidWidth(r.offset, r.width))
        return kernel::KernelError::InvalidArgument;
      uint32_t v;
      {
        kernel::IrqSpinLockGuard guard(g_configLock);
        v = RawRead(a, r.offset, r.width);
      }
      __builtin_memcpy(out.data(), &v, sizeof(v));
      transferred = sizeof(v);
      return kernel::KernelError::Success;
    }

    case kernel::pci::IOCTL_PCI_CONFIG_WRITE: {
      kernel::pci::ConfigRequest r{};
      if (in.size() < sizeof(r))
        return kernel::KernelError::InvalidArgument;
      __builtin_memcpy(&r, in.data(), sizeof(r));
      if (!ValidWidth(r.offset, r.width))
        return kernel::KernelError::InvalidArgument;
      kernel::IrqSpinLockGuard guard(g_configLock);
      RawWrite(a, r.offset, r.width, r.value);
      return kernel::KernelError::Success;
    }

    case kernel::pci::IOCTL_PCI_ENABLE_MSI:
      // x86_old has no local APIC to target, so MSI cannot be delivered.
      // A future HAL exposing an LAPIC query enables it here: address
      // 0xFEE00000 | lapicId << 12, data = vector, MSI enable, INTx disable.
      return kernel::KernelError::Unsupported;

    default:
      return kernel::KernelError::Unsupported;
  }
}

kstd::error_code PciBus_AddDevice(const kernel::KObjectPtr<kernel::KDriverObject> &driver,
                                  const kernel::KDevicePtr<kernel::KDevice> &targetPdo) {
  kstd::static_string<64> pciBusName;
  kstd::format(pciBusName, "PciFdo{}", targetPdo->Name());

  const auto pciBusFdo = KE_IO_CreateBusWithContext<PciBusContext>(driver, pciBusName);
  if (!pciBusFdo)
    return pciBusFdo.error();

  const auto attached = KE_IO_AttachFilterDevice(*pciBusFdo, targetPdo);
  if (!attached)
    return attached.error();
  return kernel::KernelError::Success;
}
// endregion
}// namespace

UNDOS_DRIVER_ENTRY {
  (void) driverConfig;

  driver->AddDevice = PciBus_AddDevice;
  driver->EnumerateDevices = Pci_EnumerateDevices;
  driver->GetChildCount = Pci_GetChildCount;
  driver->GetChild = Pci_GetChild;
  driver->GetHardwareId = Pci_GetHardwareId;
  driver->QueryResourceRequirements = Pci_QueryResourceRequirements;
  driver->DeviceControl = Pci_DeviceControl;
}
