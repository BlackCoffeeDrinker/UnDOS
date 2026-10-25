#!/usr/bin/env python3
"""Exercise the native shell through the bounded run-qemu.sh wrapper."""

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
import uuid


class Monitor:
    def __init__(self, path, mouse_x=320, mouse_y=240):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        self.mouse_x = mouse_x
        self.mouse_y = mouse_y
        self.read()
        self.command("qmp_capabilities")

    def read(self):
        line = self.stream.readline()
        if not line:
            raise RuntimeError("QMP disconnected")
        return json.loads(line)

    def command(self, name, arguments=None):
        self.stream.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
        self.stream.flush()
        while True:
            reply = self.read()
            if "error" in reply:
                raise RuntimeError(f"{name}: {reply['error']}")
            if "return" in reply:
                return reply["return"]

    def key(self, code, settle=0.06, hold_time=30):
        self.command("send-key", {"keys": [{"type": "qcode", "data": code}], "hold-time": hold_time})
        time.sleep(settle)

    def chord(self, *codes, settle=0.08, hold_time=30):
        self.command("send-key", {"keys": [{"type": "qcode", "data": code} for code in codes],
                                 "hold-time": hold_time})
        time.sleep(settle)

    def type(self, text, settle=0.06, hold_time=30):
        punctuation = {"/": "slash", ".": "dot", " ": "spc", "-": "minus"}
        for char in text:
            if char == '"':
                self.chord("shift", "apostrophe", settle=settle, hold_time=hold_time)
            elif char == "/":
                self.key("slash", settle=max(settle, 0.1), hold_time=max(hold_time, 90))
            elif char.isupper():
                self.chord("shift", char.lower(), settle=settle, hold_time=hold_time)
            else:
                self.key(punctuation.get(char, char), settle=settle, hold_time=hold_time)

    def capture(self, path):
        self.command("screendump", {"filename": str(path)})

    def move_mouse(self, x, y):
        self.command("input-send-event", {"events": [
            {"type": "rel", "data": {"axis": "x", "value": x - self.mouse_x}},
            {"type": "rel", "data": {"axis": "y", "value": y - self.mouse_y}},
        ]})
        self.mouse_x, self.mouse_y = x, y
        time.sleep(0.1)

    def click(self, x, y):
        self.move_mouse(x, y)
        self.command("input-send-event", {"events": [{"type": "btn", "data": {"down": True, "button": "left"}}]})
        time.sleep(0.05)
        self.command("input-send-event", {"events": [{"type": "btn", "data": {"down": False, "button": "left"}}]})
        time.sleep(0.1)

    def close(self):
        try:
            self.stream.close()
        except OSError:
            pass
        try:
            self.socket.close()
        except OSError:
            pass


def wait_for_markers(lines, first, markers, deadline):
    """Waits until every marker appears after line `first`; returns False at the deadline."""
    while time.monotonic() < deadline:
        recent = lines[first:]
        if all(any(marker in line for line in recent) for marker in markers):
            return True
        time.sleep(0.02)
    return False


@contextmanager
def qmp_socket_address(root):
    address = root / f"q-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    try:
        yield address
    finally:
        try:
            address.unlink()
        except FileNotFoundError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--vga", choices=("cirrus", "std"), default="cirrus")
    parser.add_argument("--scenario", choices=(
        "launch", "editing", "failure", "windows", "terminal", "terminal-stdio",
        "terminal-close", "multiple", "font", "font-default", "font-text",
        "font-desktop", "menu-font", "font-config", "font-config-missing",
        "font-config-invalid", "font-config-missing-face", "abi", "sdk", "display"),
        default="launch")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    config_scenarios = {
        "font-config", "font-config-missing", "font-config-invalid",
        "font-config-missing-face",
    }
    if args.scenario in config_scenarios and not os.environ.get("UNDOS_IMAGE_DIR"):
        parser.error(f"--scenario {args.scenario} requires UNDOS_IMAGE_DIR for an isolated test image")
    root = Path(__file__).resolve().parent.parent
    socket_root = root / "build"
    socket_root.mkdir(parents=True, exist_ok=True)
    lines = []
    ready = threading.Event()

    def collect(process):
        for line in process.stdout:
            lines.append(line)
            if "progman: session ready" in line:
                ready.set()

    with qmp_socket_address(socket_root) as address:
        environment = dict(os.environ, UNDOS_DISPLAY="none", UNDOS_VGA=args.vga,
                           UNDOS_QMP_SOCKET=str(address))
        process = subprocess.Popen([str(root / "run-qemu.sh")], cwd=root, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        collector = threading.Thread(target=collect, args=(process,))
        collector.start()
        monitor = None
        try:
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("QEMU wrapper exited before opening QMP; inspect serial.log")
                if address.exists():
                    try:
                        monitor = Monitor(address, 512, 384) if args.vga == "cirrus" else Monitor(address)
                        break
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass
                time.sleep(0.02)
            if not monitor:
                raise RuntimeError("QMP did not become responsive within two seconds")
            time.sleep(0.15)
            monitor.key("ret")  # Select GRUB's default entry without its five-second delay.
            startup_failure_markers = {
                "font-config-missing": "init: cannot open /System/FONTS.INI",
                "font-config-invalid": "init: invalid /System/FONTS.INI",
                "font-config-missing-face": "init: default font loading failed; desktop not started",
            }
            startup_failure = args.scenario in startup_failure_markers
            if startup_failure:
                marker = startup_failure_markers[args.scenario]
                deadline = time.monotonic() + 9
                while not any(marker in line for line in lines):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Expected startup failure was not reported: {marker}")
                    time.sleep(0.02)
                if args.scenario == "font-config-missing-face":
                    if not any("fontloader: start" in line for line in lines):
                        raise RuntimeError("Font Loader did not start for the valid missing-face configuration")
                    if any("fontload: default font set" in line for line in lines):
                        raise RuntimeError("Font Loader reported success for the missing configured face")
                elif any("fontloader: start" in line for line in lines):
                    raise RuntimeError("Font Loader started despite a missing or invalid configuration")
                if any("progman: session ready" in line for line in lines):
                    raise RuntimeError("Program Manager started after a font configuration failure")
                monitor.capture(output / "01-font-config-error.ppm")
            else:
                if not ready.wait(9):
                    raise RuntimeError("Program Manager did not become ready within 9 seconds")
                if not any("init: default font ready" in line for line in lines):
                    raise RuntimeError("Desktop started without successful default-font loading")
                if args.scenario == "font-config":
                    expected = (
                        'init: default font config backend=truetype regular="/System/Fonts/SSB.TTF" '
                        'bold="/System/Fonts/SSR.TTF" size=12pt ppem=16'
                    )
                    if not any(expected in line for line in lines):
                        raise RuntimeError("Init did not report the overridden font configuration")
                    if not any("fontload: default font set" in line for line in lines):
                        raise RuntimeError("Font Loader did not report the queried default font")
                    monitor.capture(output / "01-font-config-override.ppm")
                time.sleep(0.2)
                monitor.capture(output / "01-shell.ppm")
            if startup_failure:
                pass
            elif args.scenario == "font-config":
                pass
            elif args.scenario == "font-desktop":
                if not any(
                    'init: default font config backend=truetype regular="/System/Fonts/SSR.TTF" '
                    'bold="/System/Fonts/SSB.TTF" size=11pt ppem=15' in line
                    for line in lines
                ):
                    raise RuntimeError("Startup did not select the shipped 11-pt/15-ppem configuration")
                monitor.key("right")
                monitor.key("right")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("filemanager: directory ready" in line for line in lines):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("File Manager did not load its initial directory")
                    time.sleep(0.02)
                time.sleep(0.2)
                monitor.capture(output / "02-file-manager.ppm")
                monitor.key("f3")
                time.sleep(0.1)
                monitor.type("System")
                monitor.capture(output / "03-location-dialog.ppm")
                ready_count = sum("filemanager: directory ready" in line for line in lines)
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while sum("filemanager: directory ready" in line for line in lines) == ready_count:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("File Manager did not load /System from the location dialog")
                    time.sleep(0.02)
                time.sleep(0.2)
                monitor.capture(output / "04-system-directory.ppm")
                ready_count = sum("filemanager: directory ready" in line for line in lines)
                # QMP moves the pointer relatively, so adapter pointer scaling,
                # not pixel layout alone, selects these FONTS-row coordinates.
                # The path check below catches a click on a different row.
                pointer_row_y = 259 if args.vga == "std" else 231
                monitor.click(300, pointer_row_y)
                monitor.capture(output / "05-pointer-selection.ppm")
                monitor.click(300, pointer_row_y)
                deadline = time.monotonic() + 2
                while sum("filemanager: directory ready" in line for line in lines) == ready_count:
                    if time.monotonic() >= deadline:
                        monitor.capture(output / "06-pointer-open-failure.ppm")
                        raise RuntimeError("Pointer double-click did not open the painted Fonts directory row")
                    time.sleep(0.02)
                opened = [line for line in lines if "filemanager: directory ready" in line][ready_count:]
                if not any(line.rstrip().upper().endswith("/SYSTEM/FONTS") for line in opened):
                    raise RuntimeError("Pointer double-click opened a directory other than /System/FONTS")
                time.sleep(0.2)
                monitor.capture(output / "07-font-directory.ppm")
                monitor.key("f4")
                time.sleep(0.15)
                monitor.capture(output / "08-properties-dialog.ppm")
                if any("GUI:" in line and "failed" in line for line in lines):
                    raise RuntimeError("Default-font desktop operation failed; inspect serial.log")
            elif args.scenario == "menu-font":
                if not any(
                    'init: default font config backend=truetype regular="/System/Fonts/SSR.TTF" '
                    'bold="/System/Fonts/SSB.TTF" size=11pt ppem=15' in line
                    for line in lines
                ):
                    raise RuntimeError("Startup did not select the shipped 11-pt/15-ppem font")

                menu_bar_15 = 31 if args.vga == "cirrus" else 42
                run_row_15 = menu_bar_15 + 18 + 2 + 6 * 24 + 12
                menu_bar_24 = menu_bar_15 + 12
                popup_top_24 = menu_bar_24 + 18
                run_row_24 = popup_top_24 + 2 + 6 * 36 + 18

                monitor.click(500, 18)
                monitor.key("f10")
                time.sleep(0.15)
                monitor.capture(output / "02-menu-15-keyboard-open.ppm")
                monitor.move_mouse(25, run_row_15)
                monitor.capture(output / "03-menu-15-mouse-selected.ppm")
                monitor.click(25, run_row_15)
                time.sleep(0.6)
                monitor.capture(output / "04-menu-15-run-dialog.ppm")
                monitor.type("/system/fontload.elf", settle=0.01, hold_time=30)
                monitor.key("tab", settle=0.04, hold_time=50)
                monitor.type("24", settle=0.01, hold_time=30)
                time.sleep(0.15)
                monitor.capture(output / "05-menu-15-dialog-fields.ppm")
                first_loader_line = len(lines)
                monitor.key("ret", settle=0.06, hold_time=50)
                deadline = time.monotonic() + 4
                if not wait_for_markers(lines, first_loader_line, ["fontload: default font set"], deadline):
                    raise RuntimeError("Font Loader did not publish Source Sans 3 at 24 ppem")
                # Program Manager reports once its queue drains after the font change.
                if not wait_for_markers(lines, first_loader_line, ["progman: font reflow complete"], deadline):
                    raise RuntimeError("Program Manager did not finish reflowing for the 24-ppem font")
                monitor.capture(output / "06-shell-24.ppm")

                monitor.click(500, 18)
                monitor.click(25, menu_bar_24)
                monitor.capture(output / "09-menu-24-mouse-open.ppm")
                monitor.key("esc")
                monitor.key("f10")
                monitor.capture(output / "10-menu-24-keyboard.ppm")
                monitor.key("down")
                monitor.capture(output / "11-menu-24-key-selected.ppm")
                monitor.move_mouse(25, run_row_24)
                monitor.capture(output / "12-menu-24-mouse-selected.ppm")
                monitor.key("esc")
                if any("GUI:" in line and "failed" in line for line in lines):
                    raise RuntimeError("Menu-font smoke reported a GUI failure; inspect serial.log")
            elif args.scenario == "display":
                if not any("init: initial display active" in line for line in lines):
                    raise RuntimeError("init did not explicitly activate the initial display")
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                time.sleep(0.15)
                monitor.type("/System/init.elf")
                monitor.key("tab")
                monitor.type("--display-test")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("display probe: PASS" in line for line in lines):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Userland display switching probe failed")
                    time.sleep(0.02)
                if args.vga == "cirrus" and any("single-mode adapter" in line for line in lines):
                    raise RuntimeError("Cirrus probe did not exercise a runtime mode switch")
                time.sleep(0.2)
                monitor.capture(output / "02-restored-display.ppm")
            elif args.scenario == "launch":
                monitor.key("f10")
                time.sleep(0.15)
                monitor.capture(output / "02-file-menu.ppm")
                monitor.key("esc")
                monitor.key("right")
                monitor.key("ret")
                time.sleep(0.7)
                monitor.capture(output / "03-launched-app.ppm")
                if not any("demo: argv[1]=Welcome to UnDOS" in line for line in lines):
                    raise RuntimeError("Demo did not receive the configured quoted argument")
            elif args.scenario == "editing":
                monitor.key("f10")
                time.sleep(0.15)
                monitor.key("ret")
                time.sleep(0.15)
                monitor.type("Smoke")
                monitor.key("ret")
                time.sleep(0.1)
                monitor.key("f10")
                time.sleep(0.15)
                monitor.key("down")
                monitor.key("ret")
                time.sleep(0.15)
                monitor.type("Sample")
                monitor.key("tab")
                monitor.type("/System/demo.elf")
                monitor.key("tab")
                monitor.type('one "two words"')
                monitor.key("ret")
                time.sleep(0.2)
                monitor.capture(output / "02-edited-group.ppm")
                monitor.key("ret")
                deadline = time.monotonic() + 4
                while not any("demo: argv[2]=two words" in line for line in lines):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Edited program was not created/launched with its arguments")
                    time.sleep(0.02)
                time.sleep(0.3)
                monitor.capture(output / "03-edited-program.ppm")
            elif args.scenario == "sdk":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/sdkcheck.elf")
                monitor.key("ret")
                expected = ("SDK probe: installed runtime PASS", "SDK probe: shared-library cleanup PASS",
                            "SDK probe: proportional text fitting PASS",
                            "SDK probe: standard C output/input/errors PASS")
                deadline = time.monotonic() + 3
                while not all(any(marker in line for line in lines) for marker in expected):
                    if any("SDK probe: FAIL" in line for line in lines):
                        raise RuntimeError("Installed SDK runtime probe failed; inspect serial.log")
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Installed SDK runtime probe did not complete; inspect serial.log")
                    time.sleep(0.02)
                monitor.capture(output / "02-sdk-probe.ppm")
            elif args.scenario == "abi":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/init.elf")
                monitor.key("tab")
                monitor.type("--abi-test")
                monitor.key("ret")
                expected = (
                    "ABI probe: arguments/data/environment/handles/FPU PASS",
                    "ABI probe: lifecycle PASS",
                    "ABI probe: immediate exit PASS",
                    "ABI probe: exit status and missing-note rejection PASS",
                )
                deadline = time.monotonic() + 3
                while not all(any(marker in line for line in lines) for marker in expected):
                    if any("ABI probe:" in line and "FAIL" in line for line in lines):
                        raise RuntimeError("Application ABI probe failed; inspect serial.log")
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Application ABI probe did not complete; inspect serial.log")
                    time.sleep(0.02)
                monitor.capture(output / "02-abi-probe.ppm")
            elif args.scenario == "font":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/fontload.elf")
                monitor.key("tab")
                monitor.type("/System/Fonts/FTEST.TTF")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not (any("fontload: registered (rendering unavailable)" in line for line in lines) and
                           any("fontload: private face closed" in line for line in lines)):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Font Loader did not register and release its original TrueType fixture")
                    time.sleep(0.02)
                monitor.capture(output / "02-font-registered.ppm")
            elif args.scenario in ("font-default", "font-text"):
                first_loader_line = len(lines)
                monitor.click(500, 18)  # Focus the frame; a focused group child does not own F10.
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                time.sleep(0.3)
                # FAT paths are case-insensitive; lowercase avoids shift chords
                # so the input fits the bounded wrapper's time budget.
                monitor.type("/system/fontload.elf", settle=0.01)
                monitor.key("tab", settle=0.04, hold_time=50)
                if args.scenario == "font-text":
                    monitor.type("--preview", settle=0.01)
                else:
                    monitor.type("/system/fonts/ftest.ttf /system/fonts/ftest.ttf 12", settle=0.01)
                monitor.key("ret", settle=0.06, hold_time=50)
                deadline = time.monotonic() + 7
                while not (any("fontload: measured Hello" in line for line in lines[first_loader_line:]) and
                           any("fontload: default font set" in line for line in lines[first_loader_line:])):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Font Loader did not set and query the default font; inspect serial.log")
                    time.sleep(0.02)
                time.sleep(0.5)
                monitor.capture(output / "02-font-default.ppm")
                if args.scenario == "font-text":
                    deadline = time.monotonic() + 2
                    while not any("fontload: preview painted" in line for line in lines):
                        if time.monotonic() >= deadline or any("fontload: preview FAIL" in line for line in lines):
                            raise RuntimeError("Default-font SDK drawing failed; inspect serial.log")
                        time.sleep(0.02)
                    time.sleep(0.2)
                    monitor.capture(output / "03-font-text.ppm")
                    monitor.key("esc")
                    deadline = time.monotonic() + 1
                    while not any("fontload: preview closed" in line for line in lines):
                        if time.monotonic() >= deadline:
                            raise RuntimeError("Font preview did not close cleanly; inspect serial.log")
                        time.sleep(0.02)
                    monitor.key("f10")
                    monitor.key("right")
                    time.sleep(0.15)
                    monitor.capture(output / "04-font-menu.ppm")
            elif args.scenario == "failure":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/missing.elf")
                monitor.key("ret")
                time.sleep(0.5)
                monitor.capture(output / "02-launch-error.ppm")
                monitor.key("ret")
                time.sleep(0.1)
                monitor.capture(output / "03-recovered-shell.ppm")
                if not any("CreateProcess failed" in line for line in lines):
                    raise RuntimeError("Missing executable did not produce a visible launch failure")
            elif args.scenario == "windows":
                monitor.chord("alt", "spc")
                time.sleep(0.15)
                monitor.capture(output / "02-system-menu.ppm")
                for _ in range(3):
                    monitor.key("down")
                monitor.key("ret")
                time.sleep(0.15)
                monitor.capture(output / "03-minimized-group.ppm")
                monitor.key("tab")
                time.sleep(0.3)
                monitor.chord("alt", "spc")
                time.sleep(0.15)
                for _ in range(4):
                    monitor.key("down")
                monitor.key("ret")
                time.sleep(0.2)
                monitor.capture(output / "04-maximized-group.ppm")
            elif args.scenario == "multiple":
                monitor.key("right")
                monitor.key("ret")
                time.sleep(0.5)
                monitor.click(20, 15)
                time.sleep(0.2)
                monitor.click(190, 100)
                monitor.click(190, 100)
                time.sleep(0.5)
                monitor.capture(output / "02-multiple-apps.ppm")
                if sum("demo: argv[0]=" in line for line in lines) < 2:
                    raise RuntimeError("Mouse double-click did not launch a second independent instance")
                monitor.chord("alt", "spc")
                time.sleep(0.15)
                for _ in range(5):
                    monitor.key("down")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("demo: session ended" in line for line in lines) and time.monotonic() < deadline:
                    time.sleep(0.02)
                monitor.move_mouse(580, 420)
                time.sleep(0.2)
                monitor.capture(output / "03-one-app-closed.ppm")
                if sum("demo: session ended" in line for line in lines) != 1:
                    raise RuntimeError("Closing one instance did not end exactly one demo")
                monitor.chord("alt", "tab")
                time.sleep(0.3)
                monitor.type("alive")
                time.sleep(0.2)
                monitor.capture(output / "04-surviving-app.ppm")
            elif args.scenario == "terminal-close":
                monitor.key("ret")
                time.sleep(0.4)
                monitor.type("run /System/ttytest.elf --close")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("Terminal probe: waiting for input" in line for line in lines):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Terminal could not launch the close test")
                    time.sleep(0.02)
                monitor.capture(output / "02-child-waiting.ppm")
                monitor.chord("alt", "f4")
                deadline = time.monotonic() + 2
                while not any("Terminal probe: close PASS" in line for line in lines):
                    if time.monotonic() >= deadline:
                        monitor.capture(output / "close-timeout.ppm")
                        raise RuntimeError("Closing terminal did not release the child's input wait")
                    time.sleep(0.02)
                time.sleep(0.2)
                monitor.capture(output / "03-terminal-closed.ppm")
                if not any("term: session ended" in line for line in lines):
                    raise RuntimeError("Terminal did not close")
            elif args.scenario == "terminal-stdio":
                monitor.key("ret")
                time.sleep(0.4)
                monitor.type("run /System/ttytest.elf")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("Terminal probe: waiting for input" in line for line in lines):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Terminal child did not start with valid standard handles")
                    time.sleep(0.02)
                time.sleep(0.15)
                monitor.capture(output / "02-child-output.ppm")
                monitor.type("hello child")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while not any("Terminal probe: streams PASS" in line for line in lines):
                    if time.monotonic() >= deadline:
                        monitor.capture(output / "input-timeout.ppm")
                        raise RuntimeError("Terminal child did not receive its input line")
                    time.sleep(0.02)
                time.sleep(0.1)
                monitor.capture(output / "03-child-response.ppm")
                monitor.type("run /System/ttytest.elf --eof")
                monitor.key("ret")
                deadline = time.monotonic() + 2
                while sum("Terminal probe: waiting for input" in line for line in lines) < 2:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Terminal could not launch a second child")
                    time.sleep(0.02)
                monitor.chord("ctrl", "d")
                deadline = time.monotonic() + 2
                while not any("Terminal probe: EOF PASS" in line for line in lines):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Terminal child did not receive EOF")
                    time.sleep(0.02)
                time.sleep(0.1)
                monitor.type("exit")
                monitor.key("ret")
                time.sleep(0.2)
                monitor.capture(output / "04-terminal-closed.ppm")
                if not any("term: session ended" in line for line in lines):
                    raise RuntimeError("Terminal did not return to its command prompt")
            else:
                monitor.key("ret")
                time.sleep(0.5)
                monitor.type("help")
                monitor.key("ret")
                time.sleep(0.15)
                monitor.capture(output / "02-terminal.ppm")
                monitor.type("exit")
                monitor.key("ret")
                time.sleep(0.2)
                monitor.capture(output / "03-terminal-closed.ppm")
                if any("PANIC" in line for line in lines) or not any("term: session ended" in line for line in lines):
                    raise RuntimeError("Terminal did not process commands and exit cleanly")
        finally:
            if monitor:
                monitor.close()
            try:
                process.wait(timeout=17)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)
            collector.join(timeout=3)
            (output / "serial.log").write_text("".join(lines))
        if any("PANIC" in line for line in lines):
            raise RuntimeError("Kernel panic during the bounded run; inspect serial.log")
        if any("GUI: GetWndProc failed" in line or "GUI: GetMessage failed" in line for line in lines):
            raise RuntimeError("GUI dispatch failed during the bounded run; inspect serial.log")
        print(f"{args.scenario} checkpoints captured for {args.vga}: {output}")


if __name__ == "__main__":
    main()
