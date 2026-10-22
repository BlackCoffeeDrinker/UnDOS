#!/usr/bin/env python3
"""Exercise the native shell through the bounded run-qemu.sh wrapper."""

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time


class Monitor:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        self.mouse_x = 320
        self.mouse_y = 240
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

    def key(self, code):
        self.command("send-key", {"keys": [{"type": "qcode", "data": code}], "hold-time": 30})
        time.sleep(0.06)

    def chord(self, *codes):
        self.command("send-key", {"keys": [{"type": "qcode", "data": code} for code in codes],
                                 "hold-time": 30})
        time.sleep(0.08)

    def type(self, text):
        punctuation = {"/": "slash", ".": "dot", " ": "spc", "-": "minus"}
        for char in text:
            if char == '"':
                self.chord("shift", "apostrophe")
            elif char.isupper():
                self.chord("shift", char.lower())
            else:
                self.key(punctuation.get(char, char))

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
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--vga", choices=("cirrus", "std"), default="cirrus")
    parser.add_argument("--scenario", choices=("launch", "editing", "failure", "windows", "terminal", "terminal-stdio", "terminal-close", "multiple", "font", "abi", "sdk", "display"), default="launch")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    lines = []
    ready = threading.Event()

    def collect(process):
        for line in process.stdout:
            lines.append(line)
            if "progman: session ready" in line:
                ready.set()

    with tempfile.TemporaryDirectory(prefix="undos-qmp-") as directory:
        address = Path(directory) / "monitor.sock"
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
                        monitor = Monitor(address)
                        break
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass
                time.sleep(0.02)
            if not monitor:
                raise RuntimeError("QMP did not become responsive within two seconds")
            time.sleep(0.8)
            monitor.key("ret")  # Select GRUB's default entry without its five-second delay.
            if not ready.wait(9):
                raise RuntimeError("Program Manager did not become ready within 9 seconds")
            time.sleep(0.2)
            monitor.capture(output / "01-shell.ppm")
            if args.scenario == "display":
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
                time.sleep(0.6)
                monitor.capture(output / "03-edited-program.ppm")
                if not any("demo: argv[2]=two words" in line for line in lines):
                    raise RuntimeError("Edited program was not created/launched with its arguments")
            elif args.scenario == "sdk":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/sdkcheck.elf")
                monitor.key("ret")
                expected = ("SDK probe: installed runtime PASS", "SDK probe: shared-library cleanup PASS",
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
