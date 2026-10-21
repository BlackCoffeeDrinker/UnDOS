#!/usr/bin/env python3
"""Run File Manager or its filesystem probe through the bounded QEMU wrapper."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tools"))
from importlib import import_module

Monitor = import_module("progman-smoke").Monitor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", choices=("ui", "probe"), default="ui")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    lines = []
    ready = threading.Event()

    def collect(process):
        for line in process.stdout:
            lines.append(line)
            if "progman: session ready" in line:
                ready.set()

    with tempfile.TemporaryDirectory(prefix="undos-fm-") as temporary:
        address = Path(temporary) / "qmp.sock"
        env = dict(os.environ, UNDOS_IMAGE_DIR=str(args.image_dir.resolve()),
                   UNDOS_DISPLAY="none", UNDOS_QMP_SOCKET=str(address))
        process = subprocess.Popen([str(ROOT / "run-qemu.sh")], cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors="replace")
        collector = threading.Thread(target=collect, args=(process,))
        collector.start()
        monitor = None
        try:
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("Wrapper exited before QMP opened")
                if address.exists():
                    try:
                        monitor = Monitor(address)
                        break
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass
                time.sleep(0.02)
            if monitor is None:
                raise RuntimeError("QMP did not open")
            time.sleep(0.7)
            monitor.key("ret")
            if not ready.wait(8):
                raise RuntimeError("Program Manager did not become ready")
            if args.scenario == "probe":
                monitor.key("f10")
                for _ in range(6):
                    monitor.key("down")
                monitor.key("ret")
                monitor.type("/System/fmprobe.elf")
                monitor.key("ret")
            else:
                # Main opens with Terminal selected; File Manager follows Welcome.
                monitor.key("right")
                monitor.key("right")
                monitor.key("ret")
            marker = "FileManager probe: PASS" if args.scenario == "probe" else "filemanager: directory ready"
            deadline = time.monotonic() + 4
            while not any(marker in line for line in lines):
                if any("FileManager probe: FAIL" in line for line in lines):
                    raise RuntimeError("Filesystem probe failed")
                if time.monotonic() >= deadline:
                    raise RuntimeError("Application did not reach its success marker")
                time.sleep(0.02)
            time.sleep(0.2)
            monitor.capture(output / "01-application.ppm")
            if args.scenario == "ui":
                monitor.key("f5")
                monitor.key("tab")
                monitor.key("right")
                time.sleep(0.1)
                monitor.capture(output / "02-tree.ppm")
                monitor.key("tab")
                monitor.key("f3")
                monitor.key("backspace")
                monitor.type("/System")
                monitor.key("ret")
                time.sleep(0.2)
                monitor.capture(output / "03-system.ppm")
                monitor.key("f10")
                time.sleep(0.1)
                monitor.capture(output / "04-menu.ppm")
                monitor.key("esc")
                monitor.key("f1")
                time.sleep(0.1)
                monitor.capture(output / "05-help.ppm")
                monitor.key("esc")
            if any("GUI:" in line and "failed" in line for line in lines):
                raise RuntimeError("GUI operation failed; inspect serial.log")
            monitor.command("quit")
            process.wait(timeout=3)
        finally:
            if monitor:
                monitor.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
            collector.join(timeout=3)
            (output / "serial.log").write_text("".join(lines))


if __name__ == "__main__":
    main()
