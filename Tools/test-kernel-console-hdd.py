#!/usr/bin/env python3
"""Boot a disposable serial-console HDD through the bounded QEMU wrapper."""

import argparse
import hashlib
import importlib.util
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
TASK_ROOT = (ROOT / "build/bios-chs-fallback").resolve()
MILESTONES = (
    "UnDOS Kernel INIT with page size",
    "Root UUID 3937-4d14 selected volume",
    "Loading init...",
    "init: default font ready",
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


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("QMP screenshot is not a binary PPM image")
    offset = 2
    tokens = []
    while len(tokens) < 3:
        while offset < len(data) and data[offset] in b" \t\r\n":
            separator = data[offset]
            offset += 1
            if separator == ord("\r") and offset < len(data) and data[offset] == ord("\n"):
                offset += 1
        if offset < len(data) and data[offset] == ord("#"):
            end = data.find(b"\n", offset)
            if end < 0:
                raise ValueError("PPM screenshot has an incomplete comment")
            offset = end + 1
            continue
        end = offset
        while end < len(data) and data[end] not in b" \t\r\n#":
            end += 1
        if end == offset:
            raise ValueError("PPM screenshot header is malformed")
        tokens.append(data[offset:end])
        offset = end
    width, height, maximum = map(int, tokens)
    if width < 640 or height < 480 or maximum != 255:
        raise ValueError(f"Unexpected screenshot dimensions or depth: "
                         f"{width}x{height}, max={maximum}")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise ValueError("PPM screenshot has no pixel-data separator")
    separator = data[offset]
    offset += 1
    if separator == ord("\r") and offset < len(data) and data[offset] == ord("\n"):
        offset += 1
    pixels = data[offset:]
    if len(pixels) != width * height * 3:
        raise ValueError("PPM screenshot pixel data has an unexpected length")
    samples = pixels[::max(3, (len(pixels) // 4096 // 3) * 3)]
    if len(set(samples)) < 2:
        raise ValueError("QMP screenshot is uniform")
    return width, height


def validate_candidate(report, image):
    if report.get("default_menu_id") != "undos-console-vga":
        raise ValueError("Candidate does not preserve the historical menu ID")
    if image == (ROOT / "HDD_IMG.IMG").resolve() or TASK_ROOT not in image.parents:
        raise ValueError("Only a private candidate under build/bios-chs-fallback may be tested")
    if not image.is_file() or image.stat().st_nlink != 1:
        raise ValueError("Candidate must be a regular, single-link file")
    if sha256(image) != report.get("candidate_sha256"):
        raise ValueError("Candidate differs from the preparation report")

    provenance = report.get("provenance", {})
    if not isinstance(provenance.get("serial_driver"), dict):
        raise ValueError("Candidate report omits serial-driver provenance")
    manifests = provenance.get("build_manifests", {})
    kernel_record = manifests.get("kernel", {})
    direct_record = manifests.get("direct", {})
    if any(not entry.get("path") or not entry.get("sha256")
           for entry in (kernel_record, direct_record)):
        raise ValueError("Candidate report omits build-manifest provenance")
    prep = load_module(
        "console_prepare", ROOT / "Tools/prepare-kernel-console-hdd.py")
    inputs = prep.load_helper()
    _, _, _, _, current = prep.read_console_manifest_inputs(
        inputs,
        inputs.repo_path(kernel_record["path"]),
        inputs.repo_path(direct_record["path"]),
        inputs.repo_path(manifests["apps"]["path"]) if "apps" in manifests else None,
    )
    if current != provenance:
        raise ValueError("Producer, serial-driver or BootInfo-probe provenance changed")

    if set(report.get("allowed_changed_paths", [])) != set(
            prep.refresh_payloads(inputs, provenance)):
        raise ValueError("Candidate report refresh paths differ from producer inputs")
    prep.validate_refresh_inventory(
        inputs, image, set(report["original_directories"]),
        report["original_files"], provenance,
    )
    if inputs.disk_identity(image) != report.get("identity"):
        raise ValueError("Candidate disk identity differs from its report")
    config = inputs.run_bytes(
        "mtype", "-i", inputs.mtools_spec(image), "::/boot/grub/grub.cfg"
    )
    if b'--id=undos-console-vga' not in config:
        raise ValueError("Candidate lost the preserved historical menu entry")
    if inputs.sha256_bytes(config) != report.get("grub_config_sha256"):
        raise ValueError("Candidate GRUB menu differs from its report")
    return provenance, prep, inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.candidate_report.is_symlink():
        raise ValueError("Candidate report must not be a symlink")
    report_path = args.candidate_report.resolve(strict=True)
    if TASK_ROOT not in report_path.parents:
        raise ValueError("Candidate report must be under build/bios-chs-fallback")
    report = json.loads(report_path.read_text())
    candidate_path = Path(report["candidate_image"])
    if candidate_path.is_symlink():
        raise ValueError("Candidate image path must not be a symlink")
    image = candidate_path.resolve(strict=True)
    output = args.output.resolve()
    if output.parent != image.parent or output.exists() or args.output.is_symlink():
        raise ValueError("Boot evidence must be a new sibling directory of the candidate")
    provenance, prep, inputs = validate_candidate(report, image)

    output.mkdir(parents=True)
    disposable = output / "fat32.img"
    shutil.copy2(image, disposable)
    if sha256(disposable) != report["candidate_sha256"]:
        raise ValueError("Disposable image differs from the candidate")

    socket_path = ROOT / "build" / f"q-console-{os.getpid()}-{uuid.uuid4().hex[:6]}"
    if socket_path.exists():
        raise ValueError("QMP socket already exists")
    environment = dict(
        os.environ,
        UNDOS_IMAGE_DIR=str(output),
        UNDOS_QEMU_BOOT="c",
        UNDOS_QEMU_CPU="486",
        UNDOS_QEMU_MEMORY="32",
        UNDOS_QMP_SOCKET=str(socket_path),
        UNDOS_DISPLAY="none",
    )
    process = None
    monitor = None
    lines = []
    result = {
        "status": "FAIL",
        "candidate_image": str(image),
        "candidate_sha256": report["candidate_sha256"],
        "source_sha256": report["source_sha256"],
        "provenance": provenance,
        "allowed_changed_paths": report["allowed_changed_paths"],
        "boot_command": "./run-qemu.sh",
        "boot_bound_seconds": 15,
        "menu_action": "Enter the default undos-console-vga entry",
        "machine": "isapc",
        "cpu": "486",
        "memory_mib": 32,
        "boot_device": "c",
        "disposable_image_sha256": sha256(disposable),
    }

    def collect():
        with (output / "serial.log").open("w") as log:
            for line in process.stdout:
                lines.append(line)
                log.write(line)
                log.flush()

    collector = None
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
        qmp_deadline = time.monotonic() + 3
        while time.monotonic() < qmp_deadline and monitor is None:
            if process.poll() is not None:
                raise RuntimeError(
                    "Bounded QEMU wrapper exited before QMP was ready: "
                    + "".join(lines[-20:])
                )
            if socket_path.exists():
                try:
                    monitor = QMP(socket_path)
                except (ConnectionRefusedError, FileNotFoundError):
                    pass
            time.sleep(0.02)
        if monitor is None:
            raise RuntimeError("Bounded QEMU wrapper did not expose QMP")

        time.sleep(0.2)
        monitor.key("ret")
        deadline = time.monotonic() + 11
        serial_text = ""
        while time.monotonic() < deadline:
            serial_text = "".join(lines)
            if all(marker in serial_text for marker in MILESTONES):
                break
            if process.poll() is not None:
                break
            time.sleep(0.05)
        missing = [marker for marker in MILESTONES if marker not in serial_text]
        if missing:
            failure_screenshot = output / "boot-failure.ppm"
            monitor.command(
                "screendump", {"filename": str(failure_screenshot)}
            )
            result["failure_screenshot"] = {
                "path": str(failure_screenshot),
                "sha256": sha256(failure_screenshot),
            }
            monitor.command(
                "pmemsave",
                {"val": 0xB8000, "size": 4000,
                 "filename": str(output / "boot-failure-vga.bin")},
            )
            result["serial_partial"] = serial_text
            raise RuntimeError("Serial boot missed milestones: " + ", ".join(missing))
        if any(marker in serial_text for marker in
               ("Console diagnostic stop before /System/init.elf", "PANIC:",
                "System Halted")):
            raise RuntimeError("Serial boot reported an intentional stop or panic")
        positions = [serial_text.index(marker) for marker in MILESTONES]
        if positions != sorted(positions):
            raise RuntimeError("Serial boot milestones were not reached in order")

        screenshot = output / "desktop.ppm"
        monitor.command("screendump", {"filename": str(screenshot)})
        width, height = read_ppm(screenshot)
        result["screenshot"] = {
            "path": str(screenshot),
            "sha256": sha256(screenshot),
            "width": width,
            "height": height,
            "nonuniform": True,
        }

        serial_at_ready = len(serial_text)
        time.sleep(0.5)
        second = output / "desktop-after-serial.ppm"
        monitor.command("screendump", {"filename": str(second)})
        second_width, second_height = read_ppm(second)
        process.wait(timeout=18)
        collector.join(timeout=2)
        final_serial = "".join(lines)
        if any(marker in final_serial for marker in
               ("Console diagnostic stop before /System/init.elf", "PANIC:",
                "System Halted")):
            raise RuntimeError("Serial output reported a panic after desktop readiness")
        source_image = Path(report["source_image"]).resolve(strict=True)
        if (source_image != (ROOT / "HDD_IMG.IMG").resolve()
                or sha256(source_image) != report["source_sha256"]):
            raise RuntimeError("Source HDD changed during its disposable boot")
        result["post_readiness"] = {
            "screenshot_path": str(second),
            "screenshot_sha256": sha256(second),
            "width": second_width,
            "height": second_height,
            "serial_log_path": str(output / "serial.log"),
            "serial_log_sha256": sha256(output / "serial.log"),
            "serial_bytes_after_ready": max(0, len(final_serial) - serial_at_ready),
        }
        if sha256(image) != report["candidate_sha256"]:
            raise RuntimeError("Candidate changed during its disposable boot")
        _, _, _, _, current = prep.read_console_manifest_inputs(
            inputs,
            inputs.repo_path(provenance["build_manifests"]["kernel"]["path"]),
            inputs.repo_path(provenance["build_manifests"]["direct"]["path"]),
            inputs.repo_path(provenance["build_manifests"]["apps"]["path"])
            if "apps" in provenance["build_manifests"] else None,
        )
        if current != provenance:
            raise RuntimeError("Producer or probe provenance changed during boot test")

        result.update({
            "status": "PASS",
            "serial_milestones": list(MILESTONES),
            "wrapper_returncode": process.returncode,
            "candidate_unchanged": True,
            "provenance_unchanged": True,
        })
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        if monitor:
            monitor.close()
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        if collector:
            collector.join(timeout=2)
        socket_path.unlink(missing_ok=True)
        (output / "boot-report.json").write_text(json.dumps(result, indent=2) + "\n")

    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError,
            RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
