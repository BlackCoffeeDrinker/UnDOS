#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/process.h>
#include <userspace.h>
#include <undos/syscalls.h>

extern "C" const char *init_shared_message();

namespace {
void init_probe_device_control() {
  uint32_t vga = 0;
  if (UD_SYS_OpenV1("\\Device\\VGA16", FileOpenMode_Read, &vga) == SyscallReturnCode_OKAY) {
    IoctlDisplayMode mode{};
    uint32_t outLen = sizeof(mode);
    const auto ret = UD_SYS_ControlV1(vga, IoControlCode_DisplayQueryMode, nullptr, 0,
                                      reinterpret_cast<uint8_t *>(&mode), &outLen);
    crt_printf("init: VGA16 QueryMode ret=%x %dx%dx%d\n", ret, mode.width, mode.height, mode.bpp);
    const auto bad = UD_SYS_ControlV1(vga, 0xDEAD, nullptr, 0, nullptr, &outLen);
    crt_printf("init: VGA16 unknown code ret=%x\n", bad);
    (void) UD_SYS_CloseV1(vga);
  }

  uint32_t mouse = 0;
  if (UD_SYS_OpenV1("\\Device\\PointerPort0", FileOpenMode_Read, &mouse) == SyscallReturnCode_OKAY) {
    IoctlMouseConfig config{1, 60};
    uint32_t outLen = 0;
    const auto ret = UD_SYS_ControlV1(mouse, IoControlCode_MouseConfigure,
                                      reinterpret_cast<const uint8_t *>(&config), sizeof(config), nullptr, &outLen);
    crt_printf("init: PointerPort0 MouseConfigure ret=%x\n", ret);
    (void) UD_SYS_CloseV1(mouse);
  }
}

void init_probe_wait() {
  uint32_t event = 0;
  if (UD_SYS_CreateEventV1(EventFlags_ManualReset, &event) == SyscallReturnCode_OKAY) {
    const auto before = UD_SYS_WaitForObjectV1(event, 50);
    (void) UD_SYS_SetEventV1(event);
    const auto after = UD_SYS_WaitForObjectV1(event, WaitTimeout_Infinite);
    crt_printf("init: event wait before=%x after=%x\n", before, after);
    (void) UD_SYS_CloseV1(event);
  }

  uint32_t term = 0;
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
  uint32_t input = 0;
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
  (void) argc;
  (void) argv;

  crt_printf("\n=================================================\n");
  crt_printf("  UnDOS Interactive Console Shell (/System/init.elf)\n");
  crt_printf("  Virtual TTY subsystem active on stdio (handles 0, 1)\n");
  crt_printf("=================================================\n\n");
  crt_printf("%s", init_shared_message());

  char line[128];
  for (int prompt = 1; prompt <= 3; ++prompt) {
    crt_printf("undos:%d> ", prompt);
    if (crt_readline(line, sizeof(line))) {
      crt_printf("Executed: %s", line);
    } else {
      break;
    }
  }

  crt_printf("init: interactive session finished.\n");
  init_probe_device_control();
  init_probe_wait();
  init_probe_input();
  init_probe_windows();
  return 0;
}
