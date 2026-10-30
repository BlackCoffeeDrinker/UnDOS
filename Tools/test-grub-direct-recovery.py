#!/usr/bin/env python3
"""Run a bounded malformed-ELF rejection followed by direct-boot recovery."""

import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid


ROOT = Path(__file__).resolve().parent.parent
EXPECTED_ERROR = "kernel ELF: malformed ELF"
EXPECTED_RECOVERY = (
    "UDL:stub",
    "Reclaimed 15 boot info pages",
    "progman: session ready",
)
EXPECTED_RELOAD = (
    "DIRECT_RELOAD:first-prepared",
    "kernel image could not be opened",
    "DIRECT_RELOAD:failure-returned",
    "DIRECT_RELOAD:second-prepared",
    *EXPECTED_RECOVERY,
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

    def key(self, code, settle=0.015):
        self.command(
            "send-key",
            {"keys": [{"type": "qcode", "data": code}], "hold-time": 30},
        )
        time.sleep(settle)

    def capture(self, path):
        self.command("screendump", {"filename": str(path)})

    def close(self):
        self.stream.close()
        self.socket.close()


def recovery_config():
    return """\
set timeout=10
set default=0

serial --unit=0 --speed=115200
terminal_output serial

menuentry "Invalid truncated ELF" {
    insmod fat
    insmod /boot/grub/i386-pc/undos_direct.mod
    undos_direct /boot/direct/truncated.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=1234-5678 rootfs=FAT
    boot
}

menuentry "Valid direct recovery" {
    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=1234-5678 rootfs=FAT
    boot
}
"""


def reload_config():
    return """\
set timeout=10
set default=0

serial --unit=0 --speed=115200
terminal_output serial

menuentry "Direct-loader unload, failed prepare and reload" {
    insmod fat
    insmod /boot/grub/i386-pc/undos_direct.mod
    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=1234-5678 rootfs=FAT
    echo DIRECT_RELOAD:first-prepared
    undos_direct /boot/direct/missing.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=1234-5678 rootfs=FAT
    echo DIRECT_RELOAD:failure-returned
    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=1234-5678 rootfs=FAT
    echo DIRECT_RELOAD:second-prepared
    boot
}
"""


def build_scenario_iso(image_dir):
    task_build = ROOT / "build/grub-direct-loader"
    task_tmp = task_build / "tmp"
    task_tmp.mkdir(parents=True, exist_ok=True)
    image_relative = image_dir.relative_to(ROOT)
    command = [
        "docker", "run", "--rm", "--network", "none", "--read-only",
        "-e", "TMPDIR=/src/build/grub-direct-loader/tmp",
        "-v", f"{ROOT}:/src", "-w", "/src", "gcc-standalone:i686-elf",
        "grub-mkrescue",
        f"--directory=/src/Image/BootTools/grub/i386-pc",
        "-o", f"/src/{image_relative}/os.iso",
        f"/src/{image_relative}/isofiles",
    ]
    subprocess.run(command, cwd=ROOT, check=True)


def create_fixture(source_image, output, scenario):
    source_image = source_image.resolve(strict=True)
    if source_image == (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("The original HDD cannot be used for the recovery scenario")
    output = output.resolve()
    try:
        output.relative_to(ROOT / "build/grub-direct-loader")
    except ValueError as error:
        raise ValueError("Scenario output must stay under build/grub-direct-loader") from error
    image_dir = output / "image"
    if output.exists():
        raise ValueError(f"Scenario output already exists: {output}")
    output.mkdir(parents=True)
    shutil.copytree(source_image, image_dir)
    isofiles = image_dir / "isofiles"
    if scenario == "malformed-recovery":
        kernel = (isofiles / "boot/direct/kernel.elf").read_bytes()
        if len(kernel) < 16:
            raise ValueError("Kernel fixture is too short to create a malformed ELF")
        fixture = output / "truncated.elf"
        fixture.write_bytes(kernel[:12])
        shutil.copy2(fixture, isofiles / "boot/direct/truncated.elf")
        config = recovery_config()
    else:
        config = reload_config()
    (isofiles / "boot/grub/grub.cfg").write_text(config)
    build_scenario_iso(image_dir)
    return image_dir


def run_scenario(image_dir, output, cpu, memory, scenario):
    socket_path = ROOT / "build" / f"q-grub-direct-{os.getpid()}-{uuid.uuid4().hex[:6]}"
    serial_path = output / "serial.log"
    wrapper_path = output / "wrapper.log"
    lines = []
    process = None
    monitor = None
    collector = None
    environment = dict(
        os.environ,
        UNDOS_IMAGE_DIR=str(image_dir),
        UNDOS_QEMU_CPU=cpu,
        UNDOS_QEMU_BOOT="d",
        UNDOS_QEMU_MEMORY=str(memory),
        UNDOS_QMP_SOCKET=str(socket_path),
        UNDOS_DISPLAY="none",
    )

    def collect():
        for line in process.stdout:
            lines.append(line)

    try:
        process = subprocess.Popen(
            [str(ROOT / "run-qemu.sh")],
            cwd=ROOT,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
        )
        collector = threading.Thread(target=collect, daemon=True)
        collector.start()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and monitor is None:
            if process.poll() is not None:
                collector.join(timeout=1)
                raise RuntimeError(
                    "bounded QEMU wrapper exited before QMP was ready: "
                    + "".join(lines[-20:])
                )
            if socket_path.exists():
                try:
                    monitor = QMP(socket_path)
                except (ConnectionRefusedError, FileNotFoundError):
                    pass
            time.sleep(0.02)
        if monitor is None:
            raise RuntimeError("bounded QEMU wrapper did not expose QMP")

        time.sleep(0.5)
        monitor.key("ret")
        if scenario == "malformed-recovery":
            diagnostic_deadline = time.monotonic() + 2
            while time.monotonic() < diagnostic_deadline:
                if any(EXPECTED_ERROR in line for line in lines):
                    break
                time.sleep(0.02)
            if not any(EXPECTED_ERROR in line for line in lines):
                monitor.capture(output / "malformed-error-screen.ppm")
                raise RuntimeError("malformed ELF diagnostic was not observed on serial")
            if any("UDL:stub" in line for line in lines):
                raise RuntimeError("malformed input unexpectedly reached the owned transition")
            monitor.key("ret")
            time.sleep(0.2)
            monitor.key("down")
            monitor.key("ret")
            expected = (EXPECTED_ERROR, *EXPECTED_RECOVERY)
        else:
            expected = EXPECTED_RELOAD

        boot_deadline = time.monotonic() + 13
        while time.monotonic() < boot_deadline:
            if all(any(marker in line for line in lines)
                   for marker in expected):
                break
            if process.poll() is not None:
                break
            time.sleep(0.02)
        if not all(any(marker in line for line in lines) for marker in expected):
            raise RuntimeError(
                f"{scenario} did not reach all expected serial markers: "
                + ", ".join(marker for marker in expected
                             if not any(marker in line for line in lines))
            )
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
        serial_path.write_text("".join(lines))
        wrapper_path.write_text("".join(lines))
    print(f"{scenario} passed: {serial_path}")
    for marker in expected:
        print(f"Observed: {marker}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", choices=("malformed-recovery", "reload"),
                        default="malformed-recovery")
    parser.add_argument("--cpu", choices=("486", "pentium"), default="486")
    parser.add_argument("--memory", choices=(16, 32), type=int, default=32)
    args = parser.parse_args()
    image_dir = create_fixture(args.image_dir, args.output, args.scenario)
    run_scenario(image_dir, args.output.resolve(), args.cpu, args.memory,
                 args.scenario)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
