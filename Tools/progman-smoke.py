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
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        self.mouse_x = None
        self.mouse_y = None
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

    def calibrate_mouse(self, screenshot):
        # The guest initializes its pointer at the active display center.
        # QMP's PPM geometry reflects that display, not the VGA adapter's maximum.
        with screenshot.open("rb") as image:
            tokens = []
            while len(tokens) < 3:
                line = image.readline()
                if not line:
                    raise RuntimeError("Incomplete QMP screenshot header")
                tokens.extend(line.split(b"#", 1)[0].split())
        if tokens[0] != b"P6":
            raise RuntimeError("QMP screenshot is not a binary PPM")
        width, height = int(tokens[1]), int(tokens[2])
        if width <= 0 or height <= 0:
            raise RuntimeError("Invalid QMP screenshot geometry")
        self.mouse_x, self.mouse_y = width // 2, height // 2

    def move_mouse(self, x, y):
        if self.mouse_x is None or self.mouse_y is None:
            raise RuntimeError("Pointer must be calibrated before mouse input")
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


def read_ppm(path):
    with path.open("rb") as image:
        tokens = []
        while len(tokens) < 4:
            line = image.readline()
            if not line:
                raise RuntimeError(f"Incomplete PPM header: {path}")
            tokens.extend(line.split(b"#", 1)[0].split())
        magic, width, height, maximum = tokens[:4]
        pixels = image.read()
    if magic != b"P6" or maximum != b"255":
        raise RuntimeError(f"Unsupported PPM format: {path}")
    width, height = int(width), int(height)
    if len(pixels) != width * height * 3:
        raise RuntimeError(f"Invalid PPM pixel data length: {path}")
    return width, height, pixels


def count_rgb(image, bounds, color):
    width, height, pixels = image
    left, top, right, bottom = bounds
    if left < 0 or top < 0 or right > width or bottom > height:
        raise RuntimeError(f"Screenshot region is outside the image: {bounds}")
    expected = bytes(color)
    count = 0
    for y in range(top, bottom):
        offset = (y * width + left) * 3
        for _ in range(left, right):
            if pixels[offset:offset + 3] == expected:
                count += 1
            offset += 3
    return count


def count_green_pixels(image, bounds):
    width, height, pixels = image
    left, top, right, bottom = bounds
    if left < 0 or top < 0 or right > width or bottom > height:
        raise RuntimeError(f"Screenshot region is outside the image: {bounds}")
    count = 0
    for y in range(top, bottom):
        offset = (y * width + left) * 3
        for _ in range(left, right):
            red, green, blue = pixels[offset:offset + 3]
            if green > 40 and green > red * 2 and green > blue * 2:
                count += 1
            offset += 3
    return count


def count_bright_green_pixels(image, bounds):
    width, height, pixels = image
    left, top, right, bottom = bounds
    if left < 0 or top < 0 or right > width or bottom > height:
        raise RuntimeError(f"Screenshot region is outside the image: {bounds}")
    count = 0
    for y in range(top, bottom):
        offset = (y * width + left) * 3
        for _ in range(left, right):
            red, green, blue = pixels[offset:offset + 3]
            if green > 220 and red < 160 and blue < 160:
                count += 1
            offset += 3
    return count


def count_dark_pixels(image, bounds):
    width, height, pixels = image
    left, top, right, bottom = bounds
    if left < 0 or top < 0 or right > width or bottom > height:
        raise RuntimeError(f"Screenshot region is outside the image: {bounds}")
    count = 0
    for y in range(top, bottom):
        offset = (y * width + left) * 3
        for _ in range(left, right):
            red, green, blue = pixels[offset:offset + 3]
            if red < 64 and green < 64 and blue < 64:
                count += 1
            offset += 3
    return count


def assert_task_manager_views_rendered(processes, performance):
    columns = {
        "Image Name": ((48, 132, 378, 214), 50),
        "PID": ((379, 132, 418, 214), 5),
        "CPU": ((419, 132, 507, 214), 50),
        "Memory": ((508, 132, 598, 214), 20),
    }
    image_name_ink = count_dark_pixels(processes, (55, 152, 365, 320))
    missing = [
        name for name, (bounds, minimum) in columns.items()
        if count_rgb(processes, bounds, (0, 0, 0)) < minimum
    ]
    white_rows = count_rgb(processes, (42, 132, 599, 385), (255, 255, 255))
    if missing or white_rows < 1000 or image_name_ink < 20:
        raise RuntimeError(
            "Processes capture lacks its white table, image-name text, or "
            "visible column text: "
            f"missing={missing}, white row pixels={white_rows}, "
            f"image-name ink pixels={image_name_ink}"
        )

    graph_bounds = (41, 103, 599, 385)
    black_graph = count_rgb(performance, graph_bounds, (0, 0, 0))
    green_grid = count_green_pixels(performance, graph_bounds)
    green_trace = count_bright_green_pixels(performance, graph_bounds)
    white_labels = count_rgb(performance, graph_bounds, (255, 255, 255))
    if (black_graph < 10000 or green_grid < 100 or green_trace < 2 or
            white_labels < 500):
        raise RuntimeError(
            "Performance capture lacks a live graph trace or labels: "
            f"black={black_graph}, grid={green_grid}, trace={green_trace}, "
            f"white={white_labels}"
        )


def changed_pixels(left, right):
    if left[:2] != right[:2]:
        raise RuntimeError("Screenshots use different geometry")
    return sum(
        left[2][offset:offset + 3] != right[2][offset:offset + 3]
        for offset in range(0, len(left[2]), 3)
    )


def assert_task_manager_views_differ(processes_path, performance_path):
    left = read_ppm(processes_path)
    right = read_ppm(performance_path)
    if left[:2] != (640, 480) or right[:2] != (640, 480):
        raise RuntimeError(
            f"Task Manager screenshots must be 640x480, got {left[:2]} and {right[:2]}"
        )
    if left[:2] != right[:2]:
        raise RuntimeError("Task Manager views use different screenshot geometry")
    assert_task_manager_views_rendered(left, right)
    changed = changed_pixels(left, right)
    if changed < 1000:
        raise RuntimeError(
            f"Processes and Performance views are not visually distinct ({changed} pixels changed)"
        )
    print(f"Task Manager view comparison: 640x480, {changed} pixels changed")


def assert_task_probe_numbers(lines, first=0):
    fields = {
        "allocated bytes observed",
        "process bytes before allocation",
        "process bytes after allocation",
        "RAM used before allocation",
        "RAM used after allocation",
        "RAM free before allocation",
        "RAM free after allocation",
        "busy process cycles",
        "busy total cycles denominator",
        "blocked process cycles",
        "blocked total cycles denominator",
        "process bytes before free",
        "process bytes after free",
        "RAM used before free",
        "RAM used after free",
        "RAM free before free",
        "RAM free after free",
    }
    values = {}
    for line in lines[first:]:
        if "TaskProbe: PASS real snapshot, busy, block, allocate/free" in line:
            break
        marker = "TaskProbe: "
        if marker not in line or "=" not in line:
            continue
        name, value = line.split(marker, 1)[1].split("=", 1)
        if name in fields:
            try:
                values[name] = int(value.strip())
            except ValueError as error:
                raise RuntimeError(f"TaskProbe emitted a non-numeric {name}") from error
    missing = fields - values.keys()
    if missing:
        raise RuntimeError(f"TaskProbe omitted numeric results: {sorted(missing)}")
    if (values["allocated bytes observed"] != 8192 or
            values["process bytes after allocation"] -
                values["process bytes before allocation"] != 8192 or
            values["RAM used after allocation"] -
                values["RAM used before allocation"] != 8192 or
            values["RAM free before allocation"] -
                values["RAM free after allocation"] != 8192 or
            values["process bytes before free"] -
                values["process bytes after free"] != 8192 or
            values["RAM used before free"] -
                values["RAM used after free"] != 8192 or
            values["RAM free after free"] -
                values["RAM free before free"] != 8192 or
            values["process bytes after free"] !=
                values["process bytes before allocation"]):
        raise RuntimeError("TaskProbe allocation/free numeric deltas are inconsistent")
    if (not values["busy process cycles"] or
            not values["busy total cycles denominator"] or
            values["busy process cycles"] >
                values["busy total cycles denominator"] or
            not values["blocked total cycles denominator"] or
            values["blocked process cycles"] >=
                values["blocked total cycles denominator"] // 2):
        raise RuntimeError("TaskProbe busy/blocked CPU deltas are inconsistent")
    print(
        "Task Manager numeric probe: "
        f"allocated={values['allocated bytes observed']} bytes; "
        f"busy={values['busy process cycles']}/"
        f"{values['busy total cycles denominator']} cycles; "
        f"blocked={values['blocked process cycles']}/"
        f"{values['blocked total cycles denominator']} cycles; "
        f"RAM used={values['RAM used before allocation']} -> "
        f"{values['RAM used after allocation']} -> "
        f"{values['RAM used after free']} bytes"
    )


def run_task_manager_probe(monitor, lines, process, first=0):
    monitor.key("f10")
    for _ in range(6):
        monitor.key("down")
    monitor.key("ret")
    monitor.type("/System/taskprob.elf")
    monitor.key("ret")
    expected = (
        "TaskProbe: allocated bytes observed=8192",
        "TaskProbe: busy execution observed",
        "TaskProbe: blocking wait excluded",
        "TaskProbe: physical free observed",
        "TaskProbe: exit-owned allocation bytes=4096",
        "TaskProbe: PASS real snapshot, busy, block, allocate/free",
    )
    deadline = time.monotonic() + 4
    while not wait_for_markers(lines, first, expected, deadline):
        if any("TaskProbe: FAIL" in line for line in lines[first:]):
            raise RuntimeError("Task Manager accounting probe failed; inspect serial.log")
        if process.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError("Task Manager accounting probe did not complete; inspect serial.log")
        time.sleep(0.02)
    assert_task_probe_numbers(lines, first)
    ram_baseline = next(
        (int(line.rsplit("=", 1)[1]) for line in lines[first:]
         if "TaskProbe: RAM used before allocation=" in line),
        None)
    process_baseline = next(
        (int(line.rsplit("=", 1)[1]) for line in lines[first:]
         if "TaskProbe: process bytes before allocation=" in line),
        None)
    if ram_baseline is None or process_baseline is None:
        raise RuntimeError("TaskProbe did not report its initial memory usage")
    return ram_baseline, process_baseline


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
        "font-config-invalid", "font-config-missing-face", "font-config-missing-bold",
        "font-config-bitmap", "font-config-malformed", "font-upload-oom-safe", "abi", "sdk", "display",
        "task-manager", "task-manager-ui"),
        default="launch")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    config_scenarios = {
        "font-config", "font-config-missing", "font-config-invalid",
        "font-config-missing-face", "font-config-missing-bold", "font-config-bitmap",
        "font-config-malformed", "font-upload-oom-safe",
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
                        monitor = Monitor(address)
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
                "font-config-malformed": "init: default font loading failed; desktop not started",
                "font-upload-oom-safe": "init: bitmap fallback selected",
            }
            startup_failure = args.scenario in startup_failure_markers
            if startup_failure:
                marker = startup_failure_markers[args.scenario]
                deadline = time.monotonic() + 9
                while not any(marker in line for line in lines):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Expected startup failure was not reported: {marker}")
                    time.sleep(0.02)
                if args.scenario == "font-upload-oom-safe":
                    if not any("init: bitmap fallback: kernel font upload out of memory" in line for line in lines):
                        raise RuntimeError("Font upload did not report classified kernel OOM")
                    if any("PAGE FAULT" in line.upper() or "PANIC" in line.upper() for line in lines):
                        raise RuntimeError("Kernel panicked during the safe-OOM check")
                elif args.scenario == "font-config-malformed":
                    if not any("fontloader: start" in line for line in lines):
                        raise RuntimeError("Font Loader did not start for the malformed-font configuration")
                    if any("fontload: default font set" in line for line in lines):
                        raise RuntimeError("Font Loader reported success for the malformed font")
                elif any("fontloader: start" in line for line in lines):
                    raise RuntimeError("Font Loader started despite a missing or invalid configuration")
                if args.scenario != "font-upload-oom-safe" and any("progman: session ready" in line for line in lines):
                    raise RuntimeError("Program Manager started after a font configuration failure")
                if args.scenario == "font-upload-oom-safe":
                    # Readiness is optional: capture any desktop that appears,
                    # without turning safe OOM into full 8 MiB acceptance.
                    ready.wait(1)
                    time.sleep(0.1)
                    monitor.capture(output / "01-font-upload-oom-safe.ppm")
                else:
                    monitor.capture(output / "01-font-config-error.ppm")
            else:
                if not ready.wait(9):
                    raise RuntimeError("Program Manager did not become ready within 9 seconds")
                if not any("init: default font ready" in line for line in lines):
                    raise RuntimeError("Desktop started without successful default-font loading")
                if args.scenario in ("font-config-missing-face", "font-config-missing-bold"):
                    if not any("init: bitmap fallback: configured font file not found" in line for line in lines):
                        raise RuntimeError("Missing configured font did not report fallback reason")
                    if not any("init: bitmap fallback selected" in line for line in lines):
                        raise RuntimeError("Bitmap fallback was not successfully published")
                    if any("fontload: default font set" in line for line in lines):
                        raise RuntimeError("A partial scalable default was published")
                    if args.scenario == "font-config-missing-bold":
                        if not any("fontload: regular face registered" in line for line in lines):
                            raise RuntimeError("Regular face was not uploaded before bold absence")
                        if not any("fontloader: face closed" in line for line in lines):
                            raise RuntimeError("Regular face rollback did not complete")
                if args.scenario == "font-config-bitmap":
                    if not any("init: default font config backend=bitmap" in line for line in lines):
                        raise RuntimeError("Explicit bitmap configuration was not selected")
                    if any("fontloader: start" in line for line in lines):
                        raise RuntimeError("Bitmap configuration unexpectedly invoked Fontloader")
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
                monitor.calibrate_mouse(output / "01-shell.ppm")
            if startup_failure:
                pass
            elif args.scenario in ("font-config", "font-config-missing-face",
                                    "font-config-missing-bold", "font-config-bitmap"):
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
                if not wait_for_markers(lines, first_loader_line, ["fontloader: start"], deadline):
                    raise RuntimeError("Font Loader did not start the 24-ppem operation")
                # Bound publication from the point the loader begins its work.
                # The Run dialog's process launch is a separate phase.
                deadline = time.monotonic() + 4
                if not wait_for_markers(lines, first_loader_line, ["fontload: default font set"], deadline):
                    raise RuntimeError("Font Loader did not publish Source Sans 3 at 24 ppem")
                # The publication and reflow are separate bounded phases. Reuse
                # of the publication deadline can leave no time to observe the
                # queue-drained marker when publication completes near its limit.
                deadline = time.monotonic() + 4
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
            elif args.scenario == "task-manager-ui":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/taskmgr.elf")
                monitor.key("ret")
                time.sleep(1.5)
                monitor.click(80, 68)
                time.sleep(0.5)
                monitor.capture(output / "01-task-manager-processes.ppm")
                monitor.key("tab")
                monitor.key("ret")
                time.sleep(2.5)
                monitor.capture(output / "02-task-manager-performance.ppm")
                assert_task_manager_views_differ(
                    output / "01-task-manager-processes.ppm",
                    output / "02-task-manager-performance.ppm",
                )
                monitor.key("pgdn")
                monitor.key("left")
                monitor.key("down")
                monitor.key("pgdn")
                time.sleep(0.5)
                monitor.capture(output / "03-task-manager-keyboard-navigation.ppm")
                navigation_delta = changed_pixels(
                    read_ppm(output / "01-task-manager-processes.ppm"),
                    read_ppm(output / "03-task-manager-keyboard-navigation.ppm"))
                if navigation_delta < 1000:
                    raise RuntimeError("Task Manager keyboard navigation did not change the view")
                monitor.chord("alt", "f4")
                time.sleep(0.4)
                monitor.capture(output / "04-task-manager-closed.ppm")
                close_delta = changed_pixels(
                    read_ppm(output / "03-task-manager-keyboard-navigation.ppm"),
                    read_ppm(output / "04-task-manager-closed.ppm"))
                if close_delta < 1000:
                    raise RuntimeError("Alt+F4 did not close the Task Manager window")
                print(
                    "Task Manager keyboard navigation and close: "
                    f"{navigation_delta} and {close_delta} pixels changed"
                )
            elif args.scenario == "task-manager":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/taskprob.elf")
                monitor.key("ret")
                expected = (
                    "TaskProbe: allocated bytes observed=8192",
                    "TaskProbe: busy execution observed",
                    "TaskProbe: blocking wait excluded",
                    "TaskProbe: physical free observed",
                    "TaskProbe: exit-owned allocation bytes=4096",
                    "TaskProbe: PASS real snapshot, busy, block, allocate/free",
                )
                deadline = time.monotonic() + 4
                while not all(any(marker in line for line in lines) for marker in expected):
                    if any("TaskProbe: FAIL" in line for line in lines):
                        raise RuntimeError("Task Manager accounting probe failed; inspect serial.log")
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Task Manager accounting probe did not complete; inspect serial.log")
                    time.sleep(0.02)
                assert_task_probe_numbers(lines)
                monitor.capture(output / "02-taskprobe-complete.ppm")

                first_ram_baseline = [
                    int(line.rsplit("=", 1)[1])
                    for line in lines
                    if "TaskProbe: RAM used before allocation=" in line
                ]
                first_process_baseline = [
                    int(line.rsplit("=", 1)[1])
                    for line in lines
                    if "TaskProbe: process bytes before allocation=" in line
                ]
                if not first_ram_baseline or not first_process_baseline:
                    raise RuntimeError("TaskProbe did not report its initial RAM usage")
                time.sleep(0.3)
                second_probe_line = len(lines)
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/taskprob.elf")
                monitor.key("ret")
                second_expected = (
                    "TaskProbe: RAM used before allocation=",
                    "TaskProbe: process bytes before allocation=",
                    "TaskProbe: exit-owned allocation bytes=4096",
                    "TaskProbe: PASS real snapshot, busy, block, allocate/free",
                )
                deadline = time.monotonic() + 4
                while not wait_for_markers(lines, second_probe_line,
                                           second_expected, deadline):
                    if any("TaskProbe: FAIL" in line for line in lines[second_probe_line:]):
                        raise RuntimeError("Second Task Manager accounting probe failed")
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Second Task Manager probe did not complete")
                    time.sleep(0.02)
                assert_task_probe_numbers(lines, second_probe_line)
                second_ram_baseline = [
                    int(line.rsplit("=", 1)[1])
                    for line in lines[second_probe_line:]
                    if "TaskProbe: RAM used before allocation=" in line
                ]
                second_process_baseline = [
                    int(line.rsplit("=", 1)[1])
                    for line in lines[second_probe_line:]
                    if "TaskProbe: process bytes before allocation=" in line
                ]
                if (not second_process_baseline or
                        second_process_baseline[0] != first_process_baseline[0]):
                    raise RuntimeError(
                        "Process exit did not restore the next probe's owned-frame baseline: "
                        f"{first_process_baseline[0]} then "
                        f"{second_process_baseline[0] if second_process_baseline else 'missing'}"
                    )
                print(
                    "Task Manager process-exit owned-frame baseline: "
                    f"{first_process_baseline[0]} bytes then "
                    f"{second_process_baseline[0]} bytes; "
                    "global RAM-used baselines: "
                    f"{first_ram_baseline[0]} bytes then "
                    f"{second_ram_baseline[0] if second_ram_baseline else 'missing'} bytes"
                )
                monitor.capture(output / "03-taskprobe-reaped.ppm")
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
                time.sleep(0.5)
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
        if args.scenario == "font-upload-oom-safe" and any(
                "PAGE FAULT" in line.upper() or "EXCEPTION" in line.upper() for line in lines):
            raise RuntimeError("Kernel fault during the bounded safe-OOM run; inspect serial.log")
        if args.scenario in ("font-config-missing", "font-config-invalid", "font-config-malformed"):
            if any("progman: session ready" in line or "init: bitmap fallback" in line for line in lines):
                raise RuntimeError("Fatal font failure started desktop or selected bitmap fallback")
        if any("GUI: GetWndProc failed" in line or "GUI: GetMessage failed" in line for line in lines):
            raise RuntimeError("GUI dispatch failed during the bounded run; inspect serial.log")
        print(f"{args.scenario} checkpoints captured for {args.vga}: {output}")


if __name__ == "__main__":
    main()
