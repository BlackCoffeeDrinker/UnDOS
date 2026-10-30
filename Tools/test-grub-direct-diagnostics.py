#!/usr/bin/env python3
"""Capture bounded VGA stop diagnostics from a disposable HDD candidate."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
TASK_ROOT = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
PARTITION_OFFSET = 516096
STOP_NAMES = (
    "command", "loaded", "callback", "profile", "prepared", "copied",
    "parameters", "segments", "cleanup", "paging", "frame", "jump", "kernel",
)


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb")
        self.read()
        self.command("qmp_capabilities")

    def read(self):
        line = self.stream.readline()
        if not line:
            raise RuntimeError("QMP disconnected")
        return json.loads(line)

    def command(self, name, arguments=None):
        self.stream.write(
            (json.dumps({"execute": name, "arguments": arguments or {}}) + "\n")
            .encode()
        )
        self.stream.flush()
        while True:
            reply = self.read()
            if "error" in reply:
                raise RuntimeError(f"{name}: {reply['error']}")
            if "return" in reply:
                return reply["return"]

    def key(self, code):
        self.command(
            "send-key",
            {"keys": [{"type": "qcode", "data": code}], "hold-time": 30},
        )
        time.sleep(0.08)

    def monitor(self, command):
        return self.command(
            "human-monitor-command", {"command-line": command}
        )

    def close(self):
        self.stream.close()
        self.socket.close()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_vga_hold_screenshot(path):
    data = path.read_bytes()
    header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not header:
        raise RuntimeError("QEMU VGA capture has an invalid PPM header")
    width, height, maximum = map(int, header.groups())
    pixels = data[header.end():]
    if maximum != 255 or width < 640 or height < 400:
        raise RuntimeError("QEMU VGA capture is not an 80x25 text-sized screen")
    if len(pixels) != width * height * 3:
        raise RuntimeError("QEMU VGA capture has a truncated pixel buffer")
    row_start = height - 16
    red_background = 0
    for y in range(row_start, height):
        for x in range(width):
            offset = (y * width + x) * 3
            red, green, blue = pixels[offset:offset + 3]
            if red >= 100 and green < 100 and blue < 100:
                red_background += 1
    if red_background < 2000:
        raise RuntimeError(
            "VGA capture does not show the red diagnostic hold row "
            f"(found {red_background} red-background pixels)"
        )
    return {"width": width, "height": height, "red_background_pixels": red_background}


def read_vga_text(monitor):
    dump = monitor.monitor("xp /2000hx 0xb8000")
    words = []
    for line in dump.splitlines():
        if ":" not in line:
            continue
        _, values = line.split(":", 1)
        words.extend(
            int(value, 16)
            for value in re.findall(r"\b0x([0-9a-fA-F]{1,4})\b", values)
        )
    if len(words) != 2000:
        raise RuntimeError(
            f"QEMU exposed {len(words)} VGA text cells, expected 2000"
        )
    return "\n".join(
        "".join(chr(word & 0xFF) for word in words[row * 80:(row + 1) * 80])
        for row in range(25)
    )


def menu_positions(config, selected_stop):
    depth = 0
    top_index = -1
    submenu_index = None
    stop_index = None
    entry_index = 0
    inside = False
    for line in config.splitlines():
        stripped = line.strip()
        if depth == 0 and (
            stripped.startswith("menuentry ") or stripped.startswith("submenu ")
        ):
            top_index += 1
            if stripped.startswith('submenu "UnDOS - handoff STOP diagnostics ('):
                submenu_index = top_index
                inside = True
        elif inside and depth == 1 and stripped.startswith("menuentry "):
            if f"STOP {selected_stop} " in stripped:
                stop_index = entry_index
            entry_index += 1
        depth += line.count("{") - line.count("}")
        if inside and depth == 0:
            inside = False
    if submenu_index is None or stop_index is None:
        raise ValueError(f"GRUB diagnostic submenu or STOP {selected_stop} is missing")
    return submenu_index, stop_index


def run(image, output, stop, cpu, memory, expect_module_failure=False):
    image = image.resolve(strict=True)
    output = output.resolve()
    if image == (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Refusing to boot the published HDD; use a candidate copy")
    if TASK_ROOT not in image.parents or TASK_ROOT not in output.parents:
        raise ValueError("Candidate and output must remain in the task HDD build root")
    if output.exists() or output.is_symlink():
        raise ValueError(f"Output directory already exists: {output}")
    output.mkdir(parents=True)

    config = subprocess.run(
        ["mtype", "-i", f"{image}@@{PARTITION_OFFSET}", "::/boot/grub/grub.cfg"],
        check=True, stdout=subprocess.PIPE,
    ).stdout.decode("utf-8", "strict")
    submenu_index, stop_index = menu_positions(config, stop)
    qemu_image = output / "fat32.img"
    shutil.copy2(image, qemu_image)
    source_hash = sha256(image)
    if sha256(qemu_image) != source_hash:
        raise ValueError("Disposable QEMU HDD copy differs from its candidate")
    if expect_module_failure:
        corrupt_module = output / "invalid-module.bin"
        corrupt_module.write_bytes(b"not a GRUB module\n")
        try:
            subprocess.run(
                [
                    "mcopy", "-o", "-i",
                    f"{qemu_image}@@{PARTITION_OFFSET}",
                    str(corrupt_module),
                    "::/boot/grub/i386-pc/undos_direct.mod",
                ],
                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
        finally:
            corrupt_module.unlink(missing_ok=True)

    socket_path = ROOT / "build" / f"q-direct-diag-{os.getpid()}-{uuid.uuid4().hex[:6]}"
    screenshot = output / "vga-stop.ppm"
    serial_log = output / "serial.log"
    process = None
    monitor = None
    collector = None
    lines = []
    environment = dict(
        os.environ,
        UNDOS_IMAGE_DIR=str(output),
        UNDOS_QEMU_BOOT="c",
        UNDOS_QEMU_CPU=cpu,
        UNDOS_QEMU_MEMORY=str(memory),
        UNDOS_QMP_SOCKET=str(socket_path),
        UNDOS_DISPLAY="none",
    )

    def collect():
        for line in process.stdout:
            lines.append(line)

    try:
        process = subprocess.Popen(
            [str(ROOT / "run-qemu.sh")], cwd=ROOT, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            errors="replace",
        )
        collector = threading.Thread(target=collect, daemon=True)
        collector.start()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline and monitor is None:
            if process.poll() is not None:
                raise RuntimeError("Bounded QEMU wrapper exited before QMP was ready")
            if socket_path.exists():
                try:
                    monitor = QMP(socket_path)
                except (ConnectionRefusedError, FileNotFoundError):
                    pass
            time.sleep(0.02)
        if monitor is None:
            raise RuntimeError("Bounded QEMU wrapper did not expose QMP")

        time.sleep(0.2)
        for _ in range(submenu_index):
            monitor.key("down")
        monitor.key("ret")
        time.sleep(0.2)
        for _ in range(stop_index):
            monitor.key("down")
        monitor.key("ret")
        time.sleep(0.2)
        monitor.key("ret")

        manual_prompt = "Manual diagnostic only: expect a deliberate VGA hold."
        return_prompt = "Press Enter to return to the GRUB menu."
        deadline = time.monotonic() + 5
        visible_text = ""
        while time.monotonic() < deadline:
            visible_text = read_vga_text(monitor)
            if manual_prompt in visible_text:
                break
            time.sleep(0.05)
        if manual_prompt not in visible_text:
            raise RuntimeError(
                f"Manual menu prompt did not appear: {visible_text!r}"
            )

        if expect_module_failure:
            module_error = "ELF header smaller than expected."
            no_boot = (
                "Diagnostic module or preparation failed; boot was not attempted."
            )
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                visible_text = read_vga_text(monitor)
                if (
                    module_error in visible_text
                    and no_boot in visible_text
                    and return_prompt in visible_text
                ):
                    break
                time.sleep(0.05)
            if (
                module_error not in visible_text
                or no_boot not in visible_text
                or return_prompt not in visible_text
            ):
                raise RuntimeError(
                    f"Module-load error hold did not appear: {visible_text!r}"
                )
            if "UDL VGA TEST" in visible_text:
                raise RuntimeError(
                    "Diagnostic command ran after module-load failure"
                )
            if source_hash != sha256(image):
                raise RuntimeError("Module-failure test changed its source candidate")
            monitor.monitor(f"screendump {screenshot}")
            if not screenshot.is_file() or screenshot.stat().st_size < 10000:
                raise RuntimeError("QEMU did not capture the module error screen")
            try:
                monitor.command("quit")
            except (OSError, RuntimeError):
                pass
            if process:
                process.wait(timeout=3)
            if collector:
                collector.join(timeout=1)
            if any(
                marker in line
                for line in lines
                for marker in ("UDL:stub", "progman: session ready")
            ):
                raise RuntimeError(
                    "Kernel boot output appeared after the module-load error"
                )
            result = {
                "scenario": "module-load-failure",
                "candidate_sha256": source_hash,
                "disposable_copy_sha256": sha256(qemu_image),
                "expected_visible_text": (
                    f"{module_error} | {no_boot} | {return_prompt}"
                ),
                "captured_visible_text": visible_text,
                "screenshot": str(screenshot),
                "screenshot_sha256": sha256(screenshot),
            }
            (output / "result.json").write_text(
                json.dumps(result, indent=2) + "\n"
            )
            print("Captured module-load failure prompt; boot was not attempted")
            print(f"Screen: {screenshot}")
            print(f"Serial: {serial_log}")
            return

        expected = f"UDL DIAG {stop}"
        required_text = (expected, f"STOP={stop}")
        if stop == "copied":
            required_text += ("RESET AFTER OBSERVATION",)
        confirmation = "UDL VGA TEST: press Enter only if"
        deadline = time.monotonic() + 5
        visible_text = ""
        while time.monotonic() < deadline:
            visible_text = read_vga_text(monitor)
            if confirmation in visible_text:
                break
            time.sleep(0.05)
        if confirmation not in visible_text:
            raise RuntimeError(
                f"VGA confirmation prompt did not appear before input: "
                f"{visible_text!r}"
            )
        monitor.key("ret")

        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            visible_text = read_vga_text(monitor)
            if all(text in visible_text for text in required_text):
                break
            time.sleep(0.05)
        if not all(text in visible_text for text in required_text):
            raise RuntimeError(
                f"Selected VGA STOP text did not appear: {visible_text!r}"
            )

        deadline = time.monotonic() + 5
        first_eip = None
        while time.monotonic() < deadline:
            registers = monitor.monitor("info registers")
            match = re.search(r"\bEIP=([0-9a-fA-F]+)\b", registers)
            if match:
                first_eip = int(match.group(1), 16)
                time.sleep(0.2)
                second = monitor.monitor("info registers")
                second_match = re.search(r"\bEIP=([0-9a-fA-F]+)\b", second)
                if second_match and int(second_match.group(1), 16) == first_eip:
                    break
            time.sleep(0.05)
        if first_eip is None or not second_match:
            raise RuntimeError("QEMU did not expose a stable held diagnostic EIP")
        monitor.monitor(f"screendump {screenshot}")
        if not screenshot.is_file() or screenshot.stat().st_size < 10000:
            raise RuntimeError("QEMU did not capture the VGA diagnostic screen")
        screen = verify_vga_hold_screenshot(screenshot)
        disassembly_command = "x/1i" if stop == "kernel" else "xp/1i"
        held_instruction = monitor.monitor(
            f"{disassembly_command} 0x{first_eip:08x}"
        )
        if "jmp" not in held_instruction.lower():
            raise RuntimeError(
                "Stable diagnostic EIP is not decoded as the expected hold "
                f"jump at 0x{first_eip:08x}: {held_instruction.strip()!r}"
            )
        if source_hash != sha256(qemu_image):
            raise RuntimeError("QEMU changed the disposable candidate image")
        try:
            monitor.command("quit")
        except (OSError, RuntimeError):
            pass
    finally:
        if monitor:
            try:
                monitor.close()
            except OSError:
                pass
        if process and process.poll() is None:
            process.terminate()
        if process:
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
        if collector:
            collector.join(timeout=1)
        socket_path.unlink(missing_ok=True)
        serial_log.write_text("".join(lines))

    (output / "result.json").write_text(json.dumps({
        "stop": stop,
        "scenario": "vga-stop",
        "cpu": cpu,
        "memory_mib": memory,
        "candidate_sha256": source_hash,
        "stable_eip": f"0x{first_eip:08x}",
        "held_instruction": held_instruction.strip(),
        "expected_visible_text": " | ".join(required_text),
        "captured_visible_text": visible_text,
        "screen": screen,
        "screenshot": str(screenshot),
        "screenshot_sha256": sha256(screenshot),
    }, indent=2) + "\n")
    print(f"Captured VGA STOP={stop}; stable EIP=0x{first_eip:08x}")
    print(f"Screen: {screenshot}")
    print(f"Serial: {serial_log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stop", choices=STOP_NAMES, required=True)
    parser.add_argument("--cpu", choices=("486", "pentium"), default="486")
    parser.add_argument("--memory", choices=(16, 32), type=int, default=32)
    parser.add_argument(
        "--expect-module-failure",
        action="store_true",
        help="Corrupt only the disposable HDD copy and verify the menu error hold",
    )
    args = parser.parse_args()
    run(
        args.image, args.output, args.stop, args.cpu, args.memory,
        args.expect_module_failure,
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
