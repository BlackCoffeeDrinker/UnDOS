#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/process.h>
#include <undoscrt/heap.h>
#include <userspace.h>
#include <undos/syscalls.h>
#include "display_config.hpp"
#include "display_probe.hpp"
#include "font_config.hpp"

extern "C" const char *init_shared_message();

namespace {
template<typename T>
struct DisplayRecords {
  T *data{};
  uint32_t count{};
  ~DisplayRecords() { if (data) crt_free(data); }
  bool allocate(uint32_t size) {
    if (size > SIZE_MAX / sizeof(T)) return false;
    count = size;
    if (!size) return true;
    data = static_cast<T *>(crt_malloc(static_cast<size_t>(size) * sizeof(T)));
    return data != nullptr;
  }
};

template<typename T, typename Enumerate>
bool read_display_records(DisplayRecords<T> &records, Enumerate enumerate) {
  uint32_t capacity = 0, required = 0, written = 0;
  if (enumerate(1, nullptr, &capacity, &required, &written) != SyscallReturnCode_OKAY ||
      written || !records.allocate(required)) return false;
  capacity = records.count;
  return enumerate(0, records.data, &capacity, &required, &written) == SyscallReturnCode_OKAY &&
         required == records.count && written == records.count;
}

bool init_fonts() {
  HANDLE configurationFile = 0;
  if (UD_SYS_OpenV1("/System/FONTS.INI", FileOpenMode_Read, &configurationFile) !=
      SyscallReturnCode_OKAY) {
    (void)UD_SYS_PrintV1("init: cannot open /System/FONTS.INI");
    return false;
  }
  char configuration[1024]{};
  uint32_t total = 0;
  bool readFailed = false;
  while (total < sizeof(configuration)) {
    uint32_t length = sizeof(configuration) - total;
    if (UD_SYS_ReadV1(configurationFile,
                      reinterpret_cast<uint8_t *>(configuration + total), &length) !=
            SyscallReturnCode_OKAY ||
        length > sizeof(configuration) - total) {
      readFailed = true;
      break;
    }
    total += length;
    if (!length) break;
  }
  const auto closeStatus = UD_SYS_CloseV1(configurationFile);
  if (readFailed || closeStatus != SyscallReturnCode_OKAY) {
    (void)UD_SYS_PrintV1(readFailed ? "init: cannot read /System/FONTS.INI"
                                    : "init: cannot close /System/FONTS.INI");
    return false;
  }

  init::FontConfiguration fonts{};
  char arguments[1024]{};
  char diagnostic[640]{};
  if (total == sizeof(configuration) ||
      !init::parse_font_config(configuration, total, fonts) ||
      !init::make_font_config_diagnostic(fonts, diagnostic, sizeof(diagnostic))) {
    (void)UD_SYS_PrintV1("init: invalid /System/FONTS.INI");
    return false;
  }
  (void)UD_SYS_PrintV1(diagnostic);
  if (fonts.backend == init::FontBackend::BuiltinBitmap) {
    uint32_t generation = 0;
    if (UD_SYS_SetSystemFontV1(FontBackend_BackendBuiltinBitmap, 0, 0, 0,
                               &generation) != SyscallReturnCode_OKAY) {
      (void)UD_SYS_PrintV1("init: cannot publish the built-in bitmap font");
      return false;
    }
    (void)UD_SYS_PrintV1("init: default font ready");
    return true;
  }
  if (!init::make_font_loader_arguments(fonts, arguments, sizeof(arguments))) {
    (void)UD_SYS_PrintV1("init: invalid /System/FONTS.INI");
    return false;
  }
  HANDLE loader = 0;
  const auto created = UD_SYS_CreateProcessV1("/System/fontload.elf",
      arguments, ProcessFlags_None, &loader);
  if (created != SyscallReturnCode_OKAY) {
    (void)UD_SYS_PrintV1("init: cannot start default font loader");
    return false;
  }
  uint32_t exitCode = 0;
  const auto waited = UD_SYS_WaitProcessV1(loader, WaitTimeout_Infinite, &exitCode);
  const auto closed = UD_SYS_CloseV1(loader);
  if (waited != SyscallReturnCode_OKAY || closed != SyscallReturnCode_OKAY || exitCode) {
    (void)UD_SYS_PrintV1("init: default font loading failed; desktop not started");
    return false;
  }
  (void)UD_SYS_PrintV1("init: default font ready");
  return true;
}

bool init_display() {
  VideoPresentTopology topology{};
  if (UD_SYS_GetVideoPresentTopologyV1(&topology) != SyscallReturnCode_OKAY ||
      topology.targetCount == 0) {
    (void) UD_SYS_PrintV1("init: no video-present target is available");
    return false;
  }
  if (topology.pathCount == 1) {
    if (init::confirm_existing_display_path(topology, UD_SYS_EnumVideoPresentPathsV1))
      return true;
    (void) UD_SYS_PrintV1("init: existing display path is stale, degraded or inactive");
    return false;
  }
  HANDLE file = 0;
  if (UD_SYS_OpenV1("/System/DISPLAY.CFG", FileOpenMode_Read, &file) != SyscallReturnCode_OKAY) {
    (void) UD_SYS_PrintV1("init: cannot open /System/DISPLAY.CFG");
    return false;
  }
  char configuration[128]{};
  uint32_t total = 0;
  bool readable = true;
  while (total < sizeof(configuration)) {
    uint32_t length = sizeof(configuration) - total;
    if (UD_SYS_ReadV1(file, reinterpret_cast<uint8_t *>(configuration + total), &length) != SyscallReturnCode_OKAY) {
      readable = false;
      break;
    }
    total += length;
    if (!length) break;
  }
  if (UD_SYS_CloseV1(file) != SyscallReturnCode_OKAY) readable = false;
  init::DisplayPreference preference{};
  if (!readable || total == sizeof(configuration) || !init::parse_display_config(configuration, total, preference)) {
    (void) UD_SYS_PrintV1("init: DISPLAY.CFG must contain width height bpp refreshHz (refresh 0 prefers 60 Hz)");
    return false;
  }
  DisplayRecords<VideoPresentSourceMode> sourceModes;
  if (!read_display_records(sourceModes, [&](uint32_t query, VideoPresentSourceMode *data,
      uint32_t *capacity, uint32_t *required, uint32_t *written) {
        return UD_SYS_EnumVideoPresentSourceModesV1(topology.generation, 1, 0, query,
            data, capacity, required, written);
      })) {
    (void) UD_SYS_PrintV1("init: source-mode enumeration failed");
    return false;
  }
  DisplayRecords<VideoPresentTarget> targets;
  if (!read_display_records(targets, [&](uint32_t query, VideoPresentTarget *data,
      uint32_t *capacity, uint32_t *required, uint32_t *written) {
        return UD_SYS_EnumVideoPresentTargetsV1(topology.generation, 0, query,
            data, capacity, required, written);
      })) {
    (void) UD_SYS_PrintV1("init: target enumeration failed");
    return false;
  }
  for (uint32_t targetIndex = 0; targetIndex < targets.count; ++targetIndex) {
    const auto &target = targets.data[targetIndex];
    if (!target.connected) continue;
    DisplayRecords<VideoPresentTargetMode> modes;
    if (!read_display_records(modes, [&](uint32_t query, VideoPresentTargetMode *data,
        uint32_t *capacity, uint32_t *required, uint32_t *written) {
          return UD_SYS_EnumVideoPresentTargetModesV1(topology.generation, target.targetId,
              0, query, data, capacity, required, written);
        })) {
      (void) UD_SYS_PrintV1("init: target-mode enumeration failed");
      return false;
    }
    for (uint32_t modeIndex = 0; modeIndex < modes.count; ++modeIndex) {
      const auto &mode = modes.data[modeIndex];
      if (!init::matches_display_preference(preference, mode.width, mode.height,
                                            mode.colorDepth, mode.refreshHz))
        continue;
      uint32_t sourceModeId = 0;
      for (uint32_t sourceIndex = 0; sourceIndex < sourceModes.count; ++sourceIndex) {
        const auto &source = sourceModes.data[sourceIndex];
        if (source.width == mode.width && source.height == mode.height &&
            source.colorDepth == mode.colorDepth && source.pixelFormat == mode.pixelFormat) {
          sourceModeId = source.modeId;
          break;
        }
      }
      if (!sourceModeId) continue;
      const VideoPresentPath path{
          .sourceId = 1, .sourceModeId = sourceModeId, .targetId = target.targetId,
          .targetModeId = mode.modeId, .sourceX = 0, .sourceY = 0,
          .width = mode.width, .height = mode.height, .flags = 0};
      HANDLE token = 0;
      const auto validated = UD_SYS_ValidateVideoPresentTopologyV1(
          topology.generation, &path, 1, &token);
      if (validated != SyscallReturnCode_OKAY) continue;
      VideoPresentTargetResult result{};
      uint32_t resultCapacity = 1, required = 0, written = 0;
      const auto committed = UD_SYS_CommitVideoPresentTopologyV1(
          token, &result, &resultCapacity, &required, &written);
      crt_printf("init: initial display %ux%ux%u@%u ret=%x outcome=%u active=%u\n",
                 mode.width, mode.height, mode.colorDepth, mode.refreshHz,
                 committed, result.outcome, result.active);
      if (committed == SyscallReturnCode_OKAY && written == 1 &&
          result.outcome == 0 && result.active == 1) {
        (void) UD_SYS_PrintV1("init: initial display active");
        return true;
      }
      if (committed != SyscallReturnCode_OKAY) (void)UD_SYS_CloseV1(token);
      (void) UD_SYS_PrintV1("init: initial display activation failed");
      return false;
    }
  }
  (void) UD_SYS_PrintV1("init: configured fixed-refresh mode is unsupported");
  return false;
}

int init_probe_display() {
  VideoPresentTopology topology{};
  VideoPresentPath paths[1]{};
  uint32_t pathCapacity = 1, pathCount = 0, required = 0;
  uint32_t dc = 0;
  if (UD_SYS_GetVideoPresentTopologyV1(&topology) != SyscallReturnCode_OKAY ||
      topology.pathCount != 1 ||
      UD_SYS_EnumVideoPresentPathsV1(topology.generation, 0, 0, paths,
          &pathCapacity, &required, &pathCount) != SyscallReturnCode_OKAY || pathCount != 1 ||
      UD_SYS_CreateDCV1("", "", DCInitFlags_Primary, &dc) != SyscallReturnCode_OKAY) return 1;
  auto invalidPath = paths[0];
  invalidPath.flags = 1;
  HANDLE invalidToken = 0;
  bool okay = UD_SYS_ValidateVideoPresentTopologyV1(
      topology.generation, &invalidPath, 1, &invalidToken) ==
      SyscallReturnCode_INVALID_ARGUMENT;
  HANDLE token = 0;
  okay = okay && UD_SYS_ValidateVideoPresentTopologyV1(
      topology.generation, paths, 1, &token) == SyscallReturnCode_OKAY;
  VideoPresentTargetResult result{};
  uint32_t resultCapacity = 1, written = 0;
  okay = okay && UD_SYS_CommitVideoPresentTopologyV1(
      token, &result, &resultCapacity, &required, &written) == SyscallReturnCode_OKAY &&
      written == 1 && result.outcome == 0 && result.active == 1;
  DeviceCaps caps{};
  okay = okay && UD_SYS_GetDeviceCapsV1(dc, &caps) == SyscallReturnCode_OKAY &&
         caps.width == paths[0].width && caps.height == paths[0].height &&
         UD_SYS_RectangleV1(dc, 2, 2, 18, 18) == SyscallReturnCode_OKAY;
  const bool closed = UD_SYS_DeleteDCV1(dc) == SyscallReturnCode_OKAY;
  okay = okay && closed;
  (void) UD_SYS_PrintV1(okay ? "topology display probe: PASS" : "topology display probe: FAIL");
  return okay ? 0 : 1;
}

void init_probe_device_control() {
  HANDLE mouse = 0;
  if (UD_SYS_OpenV1("\\Device\\PointerPort0", FileOpenMode_Read, &mouse) == SyscallReturnCode_OKAY) {
    constexpr IoctlMouseConfig config{.enable = 1, .sampleRate = 60};
    uint32_t outLen = 0;
    const auto ret = UD_SYS_ControlV1(mouse, IoControlCode_MouseConfigure,
                                      reinterpret_cast<const uint8_t *>(&config), sizeof(config), nullptr, &outLen);
    crt_printf("init: PointerPort0 MouseConfigure ret=%x\n", ret);
    (void) UD_SYS_CloseV1(mouse);
  }
}

void init_probe_wait() {
  HANDLE event = 0;
  if (UD_SYS_CreateEventV1(EventFlags_ManualReset, &event) == SyscallReturnCode_OKAY) {
    const auto before = UD_SYS_WaitForObjectV1(event, 50);
    (void) UD_SYS_SetEventV1(event);
    const auto after = UD_SYS_WaitForObjectV1(event, WaitTimeout_Infinite);
    crt_printf("init: event wait before=%x after=%x\n", before, after);
    (void) UD_SYS_CloseV1(event);
  }

  HANDLE term = 0;
  const auto created = UD_SYS_CreateProcessV1("/System/term.elf", "", ProcessFlags_None, &term);
  if (created != SyscallReturnCode_OKAY) {
    crt_printf("init: CreateProcess term.elf ret=%x\n", created);
    return;
  }
  uint32_t exitCode = 0;
  const auto waited = UD_SYS_WaitProcessV1(term, WaitTimeout_Infinite, &exitCode);
  crt_printf("init: term.elf WaitProcess ret=%x exit=%d\n", waited, exitCode);
  (void) UD_SYS_CloseV1(term);
}

void init_probe_input() {
  HANDLE input = 0;
  const auto opened = UD_SYS_OpenV1("\\Device\\Input", FileOpenMode_Read, &input);
  if (opened != SyscallReturnCode_OKAY) {
    crt_printf("init: open Device\\Input ret=%x\n", opened);
    return;
  }
  const auto waited = UD_SYS_WaitForObjectV1(input, 100);
  crt_printf("init: Device\\Input wait ret=%x\n", waited);
  (void) UD_SYS_CloseV1(input);
}
} // namespace

namespace {
// Drains the queue without blocking, paints on WM_PAINT and logs each message.
void init_pump_windows(const char *phase) {
  Msg msg{};
  for (int i = 0; i < 32 && UD_SYS_PeekMessageV1(&msg, 1) == SyscallReturnCode_OKAY; ++i) {
    crt_printf("init: [%s] msg hwnd=%x msg=%x lParam=%x\n", phase, msg.hwnd, msg.message, msg.lParam);
    if (msg.message != WindowMessage_Paint) continue;
    uint32_t dc = 0;
    PaintInfo paint{};
    if (UD_SYS_BeginPaintV1(msg.hwnd, &dc, &paint) != SyscallReturnCode_OKAY) continue;
    crt_printf("init: [%s] paint hwnd=%x rc=%d,%d,%d,%d\n", phase, msg.hwnd, paint.rcPaint.left, paint.rcPaint.top,
               paint.rcPaint.right, paint.rcPaint.bottom);
    (void) UD_SYS_RectangleV1(dc, 10, 10, 60, 40);
    (void) UD_SYS_EndPaintV1(msg.hwnd, dc);
  }
}

void init_probe_windows() {
  const auto reg = UD_SYS_RegisterClassV1("InitTest", 0, 7);
  uint32_t a = 0;
  uint32_t b = 0;
  const auto ra = UD_SYS_CreateWindowV1("InitTest", "A", WindowStyle_Visible, 40, 40, 200, 150, 0, 0, &a);
  const auto rb = UD_SYS_CreateWindowV1("InitTest", "B", WindowStyle_Visible, 120, 90, 200, 150, 0, 0, &b);
  crt_printf("init: RegisterClass ret=%x CreateWindow A ret=%x hwnd=%x B ret=%x hwnd=%x\n", reg, ra, a, rb, b);
  init_pump_windows("create");
  const auto rm = UD_SYS_MoveWindowV1(b, 300, 200, 200, 150);
  crt_printf("init: MoveWindow B ret=%x\n", rm);
  init_pump_windows("move");
  const auto rd = UD_SYS_DestroyWindowV1(b);
  const auto rstale = UD_SYS_ShowWindowV1(b, ShowCommand_Show);
  crt_printf("init: DestroyWindow B ret=%x stale ShowWindow ret=%x\n", rd, rstale);
  init_pump_windows("destroy");
  uint32_t posted = 0;
  for (; posted < 100 && UD_SYS_PostMessageV1(a, WindowMessage_User, posted, 0) == SyscallReturnCode_OKAY; ++posted) {}
  crt_printf("init: PostMessage accepted %d of 100 before the ring was full\n", posted);
  Msg msg{};
  uint32_t drained = 0;
  while (UD_SYS_PeekMessageV1(&msg, 1) == SyscallReturnCode_OKAY && msg.message == WindowMessage_User) ++drained;
  crt_printf("init: drained %d user messages\n", drained);
}
} // namespace

extern "C" int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--abi-test")) {
    HANDLE child = 0;
    if (UD_SYS_CreateProcessV1("/System/abitest.elf", "\"two words\" \"\" last",
                              ProcessFlags_None, &child) != SyscallReturnCode_OKAY) {
      (void) UD_SYS_PrintV1("ABI probe: child creation FAIL");
      return 1;
    }
    uint32_t status = 0;
    const auto waited = UD_SYS_WaitProcessV1(child, WaitTimeout_Infinite, &status);
    (void) UD_SYS_CloseV1(child);
    if (waited != SyscallReturnCode_OKAY || status != 0x12345678) {
      (void) UD_SYS_PrintV1("ABI probe: exit status FAIL");
      return 1;
    }
    if (UD_SYS_CreateProcessV1("/System/abitest.elf", "--immediate",
                              ProcessFlags_None, &child) != SyscallReturnCode_OKAY) {
      (void) UD_SYS_PrintV1("ABI probe: immediate child creation FAIL");
      return 1;
    }
    const auto immediate_wait = UD_SYS_WaitProcessV1(child, WaitTimeout_Infinite, &status);
    (void) UD_SYS_CloseV1(child);
    if (immediate_wait != SyscallReturnCode_OKAY || status != 0x87654321u) {
      (void) UD_SYS_PrintV1("ABI probe: immediate exit status FAIL");
      return 1;
    }
    if (UD_SYS_CreateProcessV1("/System/abimiss.elf", "", ProcessFlags_None, &child) ==
        SyscallReturnCode_OKAY) {
      (void) UD_SYS_CloseV1(child);
      (void) UD_SYS_PrintV1("ABI probe: missing note accepted FAIL");
      return 1;
    }
    (void) UD_SYS_PrintV1("ABI probe: exit status and missing-note rejection PASS");
    return 0;
  }
  if (!init_display()) return 1;
  if (argc == 2 && !strcmp(argv[1], "--display-test")) return init_probe_display();
  if (argc < 2 || strcmp(argv[1], "--diagnostic") != 0) {
    init_probe_device_control();
    if (!init_fonts()) return 1;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      HANDLE shell = 0;
      const auto created = UD_SYS_CreateProcessV1("/System/progman.elf", "", ProcessFlags_None, &shell);
      if (created != SyscallReturnCode_OKAY) {
        crt_printf("init: Program Manager startup failed (%x), attempt %u/3\n", created, attempt + 1);
        continue;
      }
      uint32_t exitCode = 0;
      const auto waited = UD_SYS_WaitProcessV1(shell, WaitTimeout_Infinite, &exitCode);
      (void) UD_SYS_CloseV1(shell);
      if (waited != SyscallReturnCode_OKAY) {
        crt_printf("init: cannot supervise Program Manager (%x)\n", waited);
        return 1;
      }
      if (exitCode == 0) {
        crt_puts("init: GUI session ended normally\n");
        return 0;
      }
      crt_printf("init: Program Manager exited with %u, attempt %u/3\n", exitCode, attempt + 1);
    }
    crt_puts("init: GUI recovery limit reached. Run init --diagnostic to exercise subsystems.\n");
    return 1;
  }

  crt_printf("\n=================================================\n");
  crt_printf("  UnDOS Interactive Console Shell (/System/init.elf)\n");
  crt_printf("  Standard streams are queried through GetStdHandle\n");
  crt_printf("=================================================\n\n");
  crt_printf("%s", init_shared_message());

  init_probe_device_control();
  init_probe_wait();
  init_probe_input();
  init_probe_windows();
  return 0;
}
