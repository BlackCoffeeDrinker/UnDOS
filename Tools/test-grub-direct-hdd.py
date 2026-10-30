#!/usr/bin/env python3
"""Boot a direct-loader HDD candidate through the bounded repository wrapper."""

import argparse
import hashlib
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
PARTITION_OFFSET = 516096
ENTRY_ID = "undos-direct-cpu-safe"
MILESTONES = (
    "UDL:stub",
    "Reclaimed 15 boot info pages",
    "Root UUID 3937-4d14 selected",
    "progman: session ready",
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
        time.sleep(0.06)

    def close(self):
        self.stream.close()
        self.socket.close()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_config(image):
    result = subprocess.run(
        ["mtype", "-i", f"{image}@@{PARTITION_OFFSET}", "::/boot/grub/grub.cfg"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout.decode("utf-8", "strict")


def entry_index(config):
    entries = [
        line for line in config.splitlines()
        if line.lstrip().startswith("menuentry ")
    ]
    matches = [
        index for index, line in enumerate(entries)
        if f"--id={ENTRY_ID}" in line
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected one CPU-safe menu ID, found {len(matches)}")
    if "CPU-safe handoff diagnostic" not in entries[matches[0]]:
        raise ValueError("CPU-safe entry label is missing")
    return matches[0]


def run(image_file, output, cpu, memory):
    image_file = image_file.resolve(strict=True)
    if image_file == (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Refusing to boot the published HDD; use its candidate copy")
    task_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    output = output.resolve()
    if task_root not in output.parents or output.exists():
        raise ValueError("Output must be a new task-local HDD update directory")
    output.mkdir(parents=True)
    config = read_config(image_file)
    selected_index = entry_index(config)
    qemu_image = output / "fat32.img"
    shutil.copy2(image_file, qemu_image)
    source_hash = sha256(image_file)
    if sha256(qemu_image) != source_hash:
        raise ValueError("Disposable QEMU HDD copy does not match the candidate")

    socket_path = ROOT / "build" / f"q-direct-hdd-{os.getpid()}-{uuid.uuid4().hex[:6]}"
    serial_path = output / "serial.log"
    wrapper_path = output / "wrapper.log"
    lines = []
    process = None
    monitor = None
    collector = None
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

        time.sleep(0.2)
        for _ in range(selected_index):
            monitor.key("down")
        monitor.key("ret")

        deadline = time.monotonic() + 9
        while time.monotonic() < deadline:
            if all(any(marker in line for line in lines) for marker in MILESTONES):
                break
            if process.poll() is not None:
                break
            time.sleep(0.02)
        missing = [
            marker for marker in MILESTONES
            if not any(marker in line for line in lines)
        ]
        if missing:
            raise RuntimeError(f"CPU-safe HDD boot missed milestones: {missing}")
        if sha256(qemu_image) != source_hash:
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
        serial_path.write_text("".join(lines))
        wrapper_path.write_text("".join(lines))

    print(f"CPU-safe HDD entry boot passed: {ENTRY_ID} (menu index {selected_index})")
    print(f"Disposable HDD SHA-256: {source_hash}")
    print(f"Serial log: {serial_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", choices=("486", "pentium"), default="486")
    parser.add_argument("--memory", choices=(16, 32), type=int, default=32)
    args = parser.parse_args()
    run(args.image, args.output, args.cpu, args.memory)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
