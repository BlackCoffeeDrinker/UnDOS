#!/usr/bin/env python3
"""Assemble the boot image from explicit kernel and SDK application manifests."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-manifest", type=Path, required=True)
    parser.add_argument("--apps-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--system-files", action="append", type=Path, default=[])
    args = parser.parse_args()
    output = args.output.resolve()
    iso = output / "isofiles"
    filesystem = output / "filesystem"
    # A fresh staging tree prevents removed modules leaking into subsequent images.
    for directory in (iso, filesystem):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    kernel = json.loads(args.kernel_manifest.read_text())
    apps = json.loads(args.apps_manifest.read_text())
    for destination, source in (kernel["files"] | apps["files"]).items():
        path = Path(destination)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"Invalid image destination: {destination}")
        target = (iso if destination.startswith("boot/") else filesystem) / path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    config = filesystem / "System/Config"
    config.mkdir(parents=True, exist_ok=True)
    fonts = filesystem / "System/Fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "Apps/Progman/PROGMAN.INI", filesystem / "System")
    shutil.copy2(ROOT / "Apps/Init/DISPLAY.CFG", filesystem / "System")
    shutil.copy2(ROOT / "Apps/Init/FONTS.INI", filesystem / "System")
    for directory in args.system_files:
        for source in directory.iterdir():
            if not source.is_file():
                raise ValueError(f"Extra System inputs must be files: {source}")
            shutil.copy2(source, filesystem / "System" / source.name)
    grub = iso / "boot/grub"
    grub.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "Image/grub.cfg.in", grub / "grub.cfg")

    def run(*command):
        subprocess.run([str(value) for value in command], cwd=ROOT, check=True)

    run(sys.executable, ROOT / "Tools/create-font-fixture.py", fonts / "FTEST.TTF")
    corpus = ROOT / "Tests/Hosted/fixtures/fonts"
    for source, destination in (
        ("SourceSans3-Regular.ttf", "SSR.TTF"),
        ("SourceSans3-Bold.ttf", "SSB.TTF"),
        ("LICENSE-SourceSans.md", "OFLSS.TXT"),
    ):
        shutil.copy2(corpus / source, fonts / destination)
    run(sys.executable, ROOT / "Tools/configuration_editor/config.py",
        config / "MACHI", "author-localmachine",
        *kernel["subsystems"])
    run(sys.executable, ROOT / "Tools/make-initial-disk.py", output / "fat32.img", filesystem)
    run("grub-file", "--is-x86-multiboot", iso / "boot/undos-boot")
    run("grub-mkrescue", f"--directory={ROOT / 'Image/BootTools/grub/i386-pc'}",
        "-o", output / "os.iso", iso)


if __name__ == "__main__":
    main()
