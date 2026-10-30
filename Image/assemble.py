#!/usr/bin/env python3
"""Assemble the boot image from explicit kernel and SDK application manifests."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
from grub_direct_diagnostics_menu import render_diagnostic_submenu

GRUB_CONFIG_SHA256 = "60a7b81f39d790c28da344e0e30845bbf7ddf1a8d99c9cbf38e2b364fe2bd133"
GRUB_MODINFO_SHA256 = "b58563eb74687c2a876fef2361e530e1a2e945468272be381ede7dcaa7519cab"


def validate_module(path):
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Direct-loader artifact is not ELF32 little-endian")
    if int.from_bytes(data[16:18], "little") != 1 or int.from_bytes(data[18:20], "little") != 3:
        raise ValueError("Direct-loader artifact is not an i386 relocatable module")
    shoff = int.from_bytes(data[32:36], "little")
    shentsize = int.from_bytes(data[46:48], "little")
    shnum = int.from_bytes(data[48:50], "little")
    shstrndx = int.from_bytes(data[50:52], "little")
    if shentsize != 40 or not shnum or shnum > 4096 or shstrndx >= shnum:
        raise ValueError("Direct-loader ELF section table is invalid")
    if shoff > len(data) or shnum * shentsize > len(data) - shoff:
        raise ValueError("Direct-loader ELF section table is truncated")
    string_header = data[shoff + shstrndx * shentsize:
                         shoff + (shstrndx + 1) * shentsize]
    string_offset = int.from_bytes(string_header[16:20], "little")
    string_size = int.from_bytes(string_header[20:24], "little")
    if string_offset > len(data) or string_size > len(data) - string_offset:
        raise ValueError("Direct-loader ELF section-name table is truncated")
    names = data[string_offset:string_offset + string_size]
    sections = {}
    for index in range(shnum):
        header = data[shoff + index * shentsize:shoff + (index + 1) * shentsize]
        name_offset = int.from_bytes(header[0:4], "little")
        if name_offset >= len(names):
            raise ValueError("Direct-loader ELF section name is out of range")
        terminator = names.find(b"\0", name_offset)
        if terminator < 0:
            raise ValueError("Direct-loader ELF section name is unterminated")
        section_name = names[name_offset:terminator].decode("ascii", "strict")
        offset = int.from_bytes(header[16:20], "little")
        size = int.from_bytes(header[20:24], "little")
        if index and offset > len(data):
            raise ValueError("Direct-loader ELF section exceeds the file")
        if index and size > len(data) - offset:
            raise ValueError("Direct-loader ELF section exceeds the file")
        if section_name:
            sections[section_name] = data[offset:offset + size]
    required = {".modname", ".moddeps", ".module_license"}
    if not required.issubset(sections):
        raise ValueError("Direct-loader module metadata sections are incomplete")
    if b"undos_direct" not in sections[".modname"]:
        raise ValueError("Direct-loader module name metadata is invalid")
    if b"LICENSE=GPLv3+" not in sections[".module_license"]:
        raise ValueError("Direct-loader license metadata is invalid")
    for dependency in (b"boot", b"mmap", b"relocator", b"gzio"):
        if dependency not in sections[".moddeps"]:
            raise ValueError(f"Direct-loader dependency is missing: {dependency.decode()}")
    return len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-manifest", type=Path, required=True)
    parser.add_argument("--direct-loader-manifest", type=Path, required=True)
    parser.add_argument("--apps-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--system-files", action="append", type=Path, default=[])
    args = parser.parse_args()
    output = args.output.resolve()
    iso = output / "isofiles"
    filesystem = output / "filesystem"
    kernel = json.loads(args.kernel_manifest.read_text())
    if kernel.get("kernel_entry_stop") is True:
        raise ValueError(
            "Image assembly rejects the isolated kernel-entry diagnostic build"
        )
    direct = json.loads(args.direct_loader_manifest.read_text())
    apps = json.loads(args.apps_manifest.read_text())
    if direct.get("schema_version") != 1 or direct.get("target") != "i386-pc":
        raise ValueError("Unsupported direct-loader manifest or target")
    if direct.get("grub_version") != "2.14" or not direct.get("verified"):
        raise ValueError("Direct-loader manifest is not verified for GRUB 2.14")
    if direct.get("config_sha256") != GRUB_CONFIG_SHA256:
        raise ValueError("Direct-loader target configuration does not match the pinned config")
    artifact = direct.get("artifact", {})
    inputs = direct.get("inputs", {})

    def sha256(path):
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()

    probe_manifest_record = inputs.get("bootinfo_layout_probe_manifest", {})
    probe_manifest_relative = Path(probe_manifest_record.get("path", ""))
    if (probe_manifest_relative.is_absolute()
            or ".." in probe_manifest_relative.parts):
        raise ValueError("BootInfo probe manifest path is invalid")
    probe_manifest_path = ROOT / probe_manifest_relative
    if (not probe_manifest_path.is_file()
            or sha256(probe_manifest_path) != probe_manifest_record.get("sha256")):
        raise ValueError("BootInfo probe evidence manifest is missing or stale")
    verifier_path = ROOT / "Tools/verify-bootinfo-layout.py"
    verifier_spec = importlib.util.spec_from_file_location(
        "verify_bootinfo_layout", verifier_path
    )
    if verifier_spec is None or verifier_spec.loader is None:
        raise ValueError("Cannot load the BootInfo target-layout verifier")
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    current_probe = verifier.verify_evidence(
        probe_manifest_path, args.kernel_manifest.resolve(), ROOT
    )
    if current_probe != inputs.get("bootinfo_layout_probe"):
        raise ValueError("Direct-loader manifest has stale BootInfo probe evidence")

    module_relative = Path(artifact.get("path", ""))
    if module_relative.is_absolute() or ".." in module_relative.parts:
        raise ValueError("Direct-loader module path is invalid")
    module_source = ROOT / module_relative
    if not module_source.is_file():
        raise ValueError(f"Missing direct-loader module: {module_source}")

    def verify_record(record, algorithm="sha256"):
        relative = Path(record["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Invalid direct-loader input path: {relative}")
        path = ROOT / relative
        if sha256(path) != record.get("sha256") and algorithm == "sha256":
            raise ValueError(f"Direct-loader input hash mismatch: {path}")
        if algorithm != "sha256":
            digest = hashlib.new(algorithm)
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
            if digest.hexdigest() != record.get(algorithm):
                raise ValueError(f"Direct-loader input hash mismatch: {path}")

    if sha256(module_source) != artifact.get("sha256"):
        raise ValueError("Direct-loader module hash does not match its manifest")
    if artifact.get("size") != module_source.stat().st_size:
        raise ValueError("Direct-loader module size does not match its manifest")
    if sha256(ROOT / "Image/BootTools/grub/i386-pc/modinfo.sh") != GRUB_MODINFO_SHA256:
        raise ValueError("Shipped GRUB module options changed from the pinned input")
    if validate_module(module_source) != artifact["size"]:
        raise ValueError("Direct-loader module size validation failed")
    if direct.get("toolchain", {}).get("image") != (
            "alpine@sha256:"
            "6708ca119efec5c3fdcad5dc95d8a28a529aad8debc95872b7e12b3941882f55"
    ):
        raise ValueError("Direct-loader toolchain image is not the pinned Alpine x86 image")
    verify_record(inputs["grub_bios_apk"])
    verify_record(inputs["grub_dev_apk"])
    verify_record(inputs["apkbuild"])
    verify_record(inputs["config"])
    verify_record(inputs["target_config"])
    verify_record(inputs["shipped_modinfo"])
    verify_record(inputs["license_notice"])
    verify_record(direct.get("handoff", {})["transition_object"])
    verify_record(inputs["grub_source"], "sha512")
    for patch in inputs["patches"]:
        verify_record(patch, "sha512")
    for path, expected in inputs["module_source_files"].items():
        relative = Path(path)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Invalid direct-loader source path: {relative}")
        if sha256(ROOT / relative) != expected:
            raise ValueError(f"Direct-loader source changed after module build: {path}")
    verify_record(inputs["prepared_source_definition"])
    for path, expected in inputs["prepared_target_support"].items():
        relative = Path(path)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Invalid prepared GRUB support path: {relative}")
        if sha256(ROOT / relative) != expected:
            raise ValueError(f"Prepared GRUB target support changed: {path}")
    kernel_source = Path(kernel["files"]["boot/kernel.elf"])
    hal_source = Path(kernel["files"]["boot/hal_x86_old.elf"])
    if sha256(kernel_source) != inputs.get("kernel_sha256"):
        raise ValueError("Kernel hash does not match the direct-loader manifest")
    if sha256(hal_source) != inputs.get("hal_sha256"):
        raise ValueError("HAL hash does not match the direct-loader manifest")
    if any("undos-boot" in key or "boot_x86_old" in key for key in kernel["files"]):
        raise ValueError("Fresh images must not require the archived boot target")
    # Validate required provenance before deleting a prior staging tree.
    for directory in (iso, filesystem):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
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
    grub_config = (ROOT / "Image/grub.cfg.in").read_text()
    marker = "@DIRECT_DIAGNOSTIC_SUBMENU@"
    if grub_config.count(marker) != 1:
        raise ValueError("GRUB configuration has no unique diagnostic-menu marker")
    grub_config = grub_config.replace(
        marker,
        render_diagnostic_submenu(artifact["sha256"][:8], "1234-5678").rstrip(),
    )
    (grub / "grub.cfg").write_text(grub_config)
    direct_boot = iso / "boot/direct"
    direct_boot.mkdir(parents=True, exist_ok=True)
    shutil.copy2(kernel_source, direct_boot / "kernel.elf")
    shutil.copy2(hal_source, direct_boot / "hal_x86_old.elf")
    grub_modules = grub / "i386-pc"
    grub_modules.mkdir(parents=True, exist_ok=True)
    shutil.copy2(module_source, grub_modules / "undos_direct.mod")
    if (sha256(direct_boot / "kernel.elf") != inputs["kernel_sha256"]
            or sha256(direct_boot / "hal_x86_old.elf") != inputs["hal_sha256"]
            or sha256(grub_modules / "undos_direct.mod") != artifact["sha256"]):
        raise ValueError("Staged direct-loader image files failed hash verification")
    if (iso / "boot/undos-boot").exists():
        raise ValueError("Fresh images must not contain an undos-boot artifact")

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
    run("grub-mkrescue", f"--directory={ROOT / 'Image/BootTools/grub/i386-pc'}",
        "-o", output / "os.iso", iso)


if __name__ == "__main__":
    main()
