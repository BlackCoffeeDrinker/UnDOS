#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/process.h>
#include <userspace.h>
#include <undos/syscalls.h>

extern "C" const char *init_shared_message();

namespace {
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
  if (argc < 2 || strcmp(argv[1], "--diagnostic") != 0) {
    init_probe_device_control();
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
