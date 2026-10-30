#!/usr/bin/env python3
"""Prepare and publish a verified private direct-loader HDD image candidate."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
from grub_direct_diagnostics_menu import render_diagnostic_submenu

PARTITION_OFFSET = 516096
EXPECTED_IMAGE_SIZE = 528482304
EXPECTED_PARTITION = (6, 1008, 1031184)
EXPECTED_VOLUME_ID = "3937-4D14"
DIRECT_ENTRY = b"""

menuentry "UnDOS - direct loader" {
    insmod /boot/grub/i386-pc/undos_direct.mod
    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=3937-4D14
    boot
}
"""
CPU_SAFE_DIRECT_ENTRY = b"""

menuentry "UnDOS - direct loader (CPU-safe handoff diagnostic)" --id=undos-direct-cpu-safe {
    insmod /boot/grub/i386-pc/undos_direct.mod
    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID=3937-4D14
    boot
}
"""
DIRECT_PATHS = (
    "/boot/grub/i386-pc/undos_direct.mod",
    "/boot/direct/kernel.elf",
    "/boot/direct/hal_x86_old.elf",
)
DIAGNOSTIC_KERNEL_PATH = "/boot/direct-diag/kernel-entry-stop.elf"
DIAGNOSTIC_KERNEL_DIRECTORY = "/boot/direct-diag"
REQUIRED_BOOT_MILESTONES = (
    "UDL:stub",
    "Reclaimed",
    "Root UUID 3937-4d14 selected",
    "progman: session ready",
)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_bytes(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE).stdout


def repo_path(value):
    path = Path(value)
    if path.is_absolute() and path.parts[:2] == ("/", "src"):
        path = ROOT.joinpath(*path.parts[2:])
    elif not path.is_absolute():
        path = ROOT / path
    path = path.resolve(strict=True)
    if ROOT not in path.parents:
        raise ValueError(f"Input path is outside the task worktree: {path}")
    return path


def disk_identity(image):
    if image.is_symlink() or not image.is_file():
        raise ValueError(f"Image must be a regular non-symlink file: {image}")
    size = image.stat().st_size
    if size != EXPECTED_IMAGE_SIZE:
        raise ValueError(f"Unexpected image size: {size}")
    with image.open("rb") as source:
        mbr = source.read(512)
        source.seek(PARTITION_OFFSET)
        boot_sector = source.read(512)
        source.seek(0)
        boot_area = source.read(PARTITION_OFFSET)
    if len(mbr) != 512 or len(boot_sector) != 512:
        raise ValueError("Image or partition boot sector is truncated")
    partition = mbr[446:462]
    actual_partition = (
        partition[4],
        *struct.unpack_from("<II", partition, 8),
    )
    if partition[0] != 0x80 or actual_partition != EXPECTED_PARTITION:
        raise ValueError(f"Unexpected active partition geometry: {actual_partition}")
    if struct.unpack_from("<H", mbr, 510)[0] != 0xaa55:
        raise ValueError("Image has no valid MBR signature")
    serial = struct.unpack_from("<I", boot_sector, 39)[0]
    volume_id = f"{serial >> 16:04X}-{serial & 0xffff:04X}"
    if volume_id != EXPECTED_VOLUME_ID:
        raise ValueError(f"Unexpected FAT volume ID: {volume_id}")
    return {
        "image_size": size,
        "mbr_sha256": sha256_bytes(mbr),
        "boot_area_sha256": sha256_bytes(boot_area),
        "partition": {
            "type": actual_partition[0],
            "start_lba": actual_partition[1],
            "sectors": actual_partition[2],
        },
        "volume_id": volume_id,
    }


def require_exclusive_image_access(image):
    result = subprocess.run(
        ["lsof", "-t", "--", str(image)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        raise ValueError(f"Image is open by another process: {image}")
    if result.returncode not in (0, 1):
        raise ValueError(f"Could not check for concurrent image use: {result.stderr}")


def mtools_spec(image):
    return f"{image}@@{PARTITION_OFFSET}"


def list_files(image):
    pending = ["/"]
    directories = set()
    files = set()
    while pending:
        directory = pending.pop()
        listing = run_bytes(
            "mdir", "-b", "-i", mtools_spec(image), f"::{directory}"
        )
        for raw in listing.decode("utf-8", "strict").splitlines():
            item = raw.strip()
            if not item.startswith("::/"):
                raise ValueError(f"Unexpected mtools path: {item!r}")
            path = item[2:]
            if path.endswith("/"):
                path = path.rstrip("/")
                if path not in directories:
                    directories.add(path)
                    pending.append(path)
            else:
                files.add(path)
    return directories, files


def inventory(image):
    directories, files = list_files(image)
    hashes = {}
    for path in sorted(files):
        data = run_bytes("mtype", "-i", mtools_spec(image), f"::{path}")
        hashes[path] = {"sha256": sha256_bytes(data), "size": len(data)}
    return directories, hashes


def check_installed_grub_modules(image, files):
    prefix = "/boot/grub/i386-pc/"
    repository = ROOT / "Image/BootTools/grub/i386-pc"
    repo_files = {
        path.name.lower(): sha256_file(path)
        for path in repository.iterdir()
        if path.is_file()
    }
    checked = 0
    for path, record in files.items():
        if not path.lower().startswith(prefix) or not path.lower().endswith(".mod"):
            continue
        name = path[len(prefix):].lower()
        if name not in repo_files:
            raise ValueError(f"Installed GRUB file is absent from matched set: {path}")
        if record["sha256"] != repo_files[name]:
            raise ValueError(f"Installed GRUB file differs from matched set: {path}")
        checked += 1
    if checked == 0:
        raise ValueError("No installed GRUB i386-pc files were found")
    return checked


def read_manifest_inputs(kernel_manifest_path, direct_manifest_path):
    kernel_manifest = json.loads(kernel_manifest_path.read_text())
    direct_manifest = json.loads(direct_manifest_path.read_text())
    if (direct_manifest.get("schema_version") != 1
            or direct_manifest.get("target") != "i386-pc"
            or direct_manifest.get("grub_version") != "2.14"
            or not direct_manifest.get("verified")):
        raise ValueError("Direct-loader manifest is not verified for GRUB 2.14")
    probe_record = direct_manifest.get("inputs", {}).get(
        "bootinfo_layout_probe_manifest", {}
    )
    if not probe_record.get("path") or not probe_record.get("sha256"):
        raise ValueError("Direct-loader manifest omits BootInfo probe provenance")
    probe_manifest_path = repo_path(probe_record.get("path", ""))
    if sha256_file(probe_manifest_path) != probe_record.get("sha256"):
        raise ValueError("BootInfo probe manifest is missing or stale")
    verifier_path = ROOT / "Tools/verify-bootinfo-layout.py"
    verifier_spec = importlib.util.spec_from_file_location(
        "verify_bootinfo_layout", verifier_path
    )
    if verifier_spec is None or verifier_spec.loader is None:
        raise ValueError("Cannot load the BootInfo target-layout verifier")
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    current_probe = verifier.verify_evidence(
        probe_manifest_path, kernel_manifest_path, ROOT
    )
    if current_probe != direct_manifest["inputs"].get("bootinfo_layout_probe"):
        raise ValueError("Direct-loader manifest has stale BootInfo probe evidence")
    module = repo_path(direct_manifest["artifact"]["path"])
    kernel = repo_path(kernel_manifest["files"]["boot/kernel.elf"])
    hal = repo_path(kernel_manifest["files"]["boot/hal_x86_old.elf"])
    for path, expected in (
        (module, direct_manifest["artifact"]["sha256"]),
        (kernel, direct_manifest["inputs"]["kernel_sha256"]),
        (hal, direct_manifest["inputs"]["hal_sha256"]),
    ):
        if sha256_file(path) != expected:
            raise ValueError(f"Manifest hash mismatch: {path}")
    return module, kernel, hal, {
        "module": direct_manifest["artifact"]["sha256"],
        "kernel": direct_manifest["inputs"]["kernel_sha256"],
        "hal": direct_manifest["inputs"]["hal_sha256"],
        "build_manifests": {
            "kernel": {
                "path": kernel_manifest_path.resolve().relative_to(ROOT).as_posix(),
                "sha256": sha256_file(kernel_manifest_path),
            },
            "direct": {
                "path": direct_manifest_path.resolve().relative_to(ROOT).as_posix(),
                "sha256": sha256_file(direct_manifest_path),
            },
        },
    }


def read_diagnostic_kernel(manifest_path):
    manifest_path = repo_path(manifest_path)
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("kernel_entry_stop") is not True:
        raise ValueError(
            "Kernel diagnostic manifest does not declare kernel_entry_stop=true"
        )
    artifact_record = manifest.get("artifact_manifest", {})
    probe_record = manifest.get("bootinfo_layout_probe_manifest", {})
    if any(not record.get("path") or not record.get("sha256")
           for record in (artifact_record, probe_record)):
        raise ValueError("Kernel diagnostic manifest omits build or probe provenance")
    artifact_manifest = repo_path(artifact_record["path"])
    probe_manifest = repo_path(probe_record["path"])
    if (sha256_file(artifact_manifest) != artifact_record["sha256"]
            or sha256_file(probe_manifest) != probe_record["sha256"]):
        raise ValueError("Kernel diagnostic build or probe manifest is stale")
    artifact_data = json.loads(artifact_manifest.read_text())
    if (artifact_data.get("kernel_entry_stop") is not True
            or artifact_data.get("files") != manifest.get("files")):
        raise ValueError("Kernel diagnostic wrapper differs from its CMake manifest")
    verifier_path = ROOT / "Tools/verify-bootinfo-layout.py"
    verifier_spec = importlib.util.spec_from_file_location(
        "verify_bootinfo_layout", verifier_path
    )
    if verifier_spec is None or verifier_spec.loader is None:
        raise ValueError("Cannot load the target BootInfo layout verifier")
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    current_probe = verifier.verify_evidence(
        probe_manifest, artifact_manifest, ROOT
    )
    if current_probe != manifest.get("bootinfo_layout_probe"):
        raise ValueError("Kernel diagnostic BootInfo probe evidence is stale")
    files = manifest.get("files", {})
    if "boot/kernel.elf" not in files:
        raise ValueError("Kernel diagnostic manifest omits its kernel artifact")
    kernel = repo_path(files["boot/kernel.elf"])
    with kernel.open("rb") as source:
        header = source.read(20)
    if (len(header) < 20 or header[:7] != b"\x7fELF\x01\x01\x01"
            or int.from_bytes(header[16:18], "little") != 2
            or int.from_bytes(header[18:20], "little") != 3):
        raise ValueError("Kernel diagnostic artifact is not ELF32 i386 executable")
    return kernel, {
        "kernel_entry_stop": True,
        "manifest_path": manifest_path.relative_to(ROOT).as_posix(),
        "manifest_sha256": sha256_file(manifest_path),
        "artifact_path": kernel.relative_to(ROOT).as_posix(),
        "artifact_sha256": sha256_file(kernel),
        "artifact_manifest_path": artifact_manifest.relative_to(ROOT).as_posix(),
        "artifact_manifest_sha256": artifact_record["sha256"],
        "probe_manifest_path": probe_manifest.relative_to(ROOT).as_posix(),
        "probe_manifest_sha256": probe_record["sha256"],
    }


def verify_kernel_stop_pairing(config, diagnostic_kernel_enabled):
    kernel_path = DIAGNOSTIC_KERNEL_PATH.encode()
    kernel_command = b"undos_direct_diag kernel "
    if not diagnostic_kernel_enabled:
        if kernel_path in config or kernel_command in config:
            raise ValueError(
                "Kernel STOP menu requires the explicit diagnostic-kernel manifest"
            )
        return
    if (config.count(kernel_path) != 1
            or config.count(kernel_command) != 1
            or kernel_command + kernel_path not in config):
        raise ValueError(
            "Kernel diagnostic payload must be used only by the single kernel STOP entry"
        )


def verify_recorded_manifests(report):
    build_manifests = report.get("build_manifests", {})
    kernel_record = build_manifests.get("kernel", {})
    direct_record = build_manifests.get("direct", {})
    if any(not record.get("path") or not record.get("sha256")
           for record in (kernel_record, direct_record)):
        raise ValueError("HDD candidate report omits build-manifest provenance")
    kernel_manifest_path = repo_path(kernel_record.get("path", ""))
    direct_manifest_path = repo_path(direct_record.get("path", ""))
    if (sha256_file(kernel_manifest_path) != kernel_record.get("sha256")
            or sha256_file(direct_manifest_path) != direct_record.get("sha256")):
        raise ValueError("Build manifests changed after HDD candidate preparation")
    _, _, _, hashes = read_manifest_inputs(
        kernel_manifest_path, direct_manifest_path
    )
    for path, key in zip(DIRECT_PATHS, ("module", "kernel", "hal")):
        if report.get("new_payload_hashes", {}).get(path) != hashes[key]:
            raise ValueError(f"Candidate payload no longer matches current build: {path}")
    if report.get("update_kind") == "kernel-entry-stop-diagnostics":
        diagnostic_record = build_manifests.get("kernel_entry_stop", {})
        if (not diagnostic_record.get("path")
                or not diagnostic_record.get("sha256")):
            raise ValueError("HDD candidate omits kernel diagnostic manifest provenance")
        diagnostic_manifest = repo_path(diagnostic_record["path"])
        if sha256_file(diagnostic_manifest) != diagnostic_record["sha256"]:
            raise ValueError("Kernel diagnostic manifest changed after candidate preparation")
        _, diagnostic = read_diagnostic_kernel(diagnostic_manifest)
        if (diagnostic["manifest_sha256"] != diagnostic_record["sha256"]
                or diagnostic != report.get("kernel_entry_stop_manifest")
                or diagnostic["artifact_sha256"]
                != report.get("new_payload_hashes", {}).get(DIAGNOSTIC_KERNEL_PATH)
                or diagnostic["artifact_sha256"][:8]
                != report.get("kernel_entry_stop_build_id")):
            raise ValueError("Kernel diagnostic payload differs from its manifest")


def prepare(args):
    if args.image.is_symlink():
        raise ValueError("Source image path must not be a symlink")
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve()
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Source image must be this task's HDD_IMG.IMG")
    if candidate.parent != work_root or candidate.exists() or candidate.is_symlink():
        raise ValueError("Candidate must be a new direct child of the HDD update build root")
    work_root.mkdir(parents=True, exist_ok=True)
    if work_root.stat().st_dev != image.stat().st_dev:
        raise ValueError("Candidate and original must be on the same filesystem")
    if (work_root / "candidate-report.json").exists():
        raise ValueError("A candidate report already exists; use a new task-local build root")
    free = shutil.disk_usage(work_root).free
    if free < image.stat().st_size * 2:
        raise ValueError("Insufficient free space for candidate and retained backup")

    require_exclusive_image_access(image)
    identity = disk_identity(image)
    directories, original_files = inventory(image)
    grub_count = check_installed_grub_modules(image, original_files)
    config_path = "/boot/grub/grub.cfg"
    if config_path not in original_files:
        raise ValueError("Installed GRUB configuration is missing")
    config = run_bytes("mtype", "-i", mtools_spec(image), f"::{config_path}")
    if b'menuentry "UnDOS - direct loader"' in config:
        raise ValueError("A direct-loader menu entry already exists")

    module, kernel, hal, hashes = read_manifest_inputs(
        args.kernel_manifest.resolve(strict=True),
        args.direct_manifest.resolve(strict=True),
    )
    existing_paths = {path.lower() for path in original_files}
    new_paths = (
        "/boot/grub/i386-pc/undos_direct.mod",
        "/boot/direct/kernel.elf",
        "/boot/direct/hal_x86_old.elf",
    )
    for path in new_paths:
        if path.lower() in existing_paths:
            raise ValueError(f"Refusing to overwrite existing HDD path: {path}")

    shutil.copy2(image, candidate)
    try:
        current_dirs, _ = list_files(candidate)
        for directory in ("/boot/direct",):
            if directory not in current_dirs:
                subprocess.run(
                    ["mmd", "-i", mtools_spec(candidate), f"::{directory}"],
                    check=True,
                )
        for source, destination in (
            (module, new_paths[0]),
            (kernel, new_paths[1]),
            (hal, new_paths[2]),
        ):
            subprocess.run(
                ["mcopy", "-i", mtools_spec(candidate), str(source),
                 f"::{destination}"],
                check=True,
            )

        config_temp = work_root / "grub.cfg.candidate"
        config_temp.write_bytes(config + DIRECT_ENTRY)
        try:
            subprocess.run(
                ["mcopy", "-o", "-i", mtools_spec(candidate), str(config_temp),
                 f"::{config_path}"],
                check=True,
            )
        finally:
            config_temp.unlink(missing_ok=True)

        candidate_identity = disk_identity(candidate)
        candidate_dirs, candidate_files = inventory(candidate)
        if candidate_identity != identity:
            raise ValueError("MBR, partition, volume ID, image length, or boot area changed")
        if not directories.issubset(candidate_dirs):
            raise ValueError("Candidate removed an existing filesystem directory")
        for path, record in original_files.items():
            if path == config_path:
                continue
            if candidate_files.get(path) != record:
                raise ValueError(f"Candidate changed or removed existing file: {path}")
        updated_config = run_bytes(
            "mtype", "-i", mtools_spec(candidate), f"::{config_path}"
        )
        if updated_config != config + DIRECT_ENTRY:
            raise ValueError("Candidate GRUB configuration is not an append-only update")
        for path, expected in zip(new_paths, (hashes["module"], hashes["kernel"],
                                              hashes["hal"])):
            data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
            if sha256_bytes(data) != expected:
                raise ValueError(f"Candidate payload hash mismatch: {path}")

        report = {
            "schema_version": 1,
            "source_image": str(image),
            "source_image_sha256": sha256_file(image),
            "candidate_image": str(candidate),
            "candidate_image_sha256": sha256_file(candidate),
            "identity": identity,
            "original_directories": sorted(directories),
            "installed_grub_files_compared": grub_count,
            "original_files": original_files,
            "new_payload_hashes": dict(zip(new_paths, (
                hashes["module"], hashes["kernel"], hashes["hal"]
            ))),
            "build_manifests": hashes["build_manifests"],
            "config_append_sha256": sha256_bytes(DIRECT_ENTRY),
            "candidate_files": len(candidate_files),
            "candidate_directories": len(candidate_dirs),
        }
        report_path = work_root / "candidate-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Candidate verified: {candidate}")
        print(f"Original files preserved: {len(original_files) - 1}; "
              f"GRUB files matched: {grub_count}")
        print(f"Candidate SHA-256: {report['candidate_image_sha256']}")
        print(f"Report: {report_path}")
    except BaseException:
        candidate.unlink(missing_ok=True)
        raise


def prepare_update(args):
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve()
    baseline_path = args.baseline_report.resolve(strict=True)
    kernel_manifest_path = args.kernel_manifest.resolve(strict=True)
    direct_manifest_path = args.direct_manifest.resolve(strict=True)
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    report_path = args.report.resolve()
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Update source must be this task's HDD_IMG.IMG")
    if candidate.parent != work_root or candidate.exists() or candidate.is_symlink():
        raise ValueError("Update candidate must be a new direct child of the HDD update build root")
    if report_path.parent != work_root or report_path.exists() or report_path.is_symlink():
        raise ValueError("Update report must be a new direct child of the HDD update build root")
    if baseline_path != (work_root / "candidate-report.json").resolve():
        raise ValueError("Update baseline must be the original task candidate report")

    baseline = json.loads(baseline_path.read_text())
    if baseline.get("candidate_image_sha256") != sha256_file(image):
        raise ValueError("Published HDD does not match the recorded prior task candidate")
    if baseline.get("config_append_sha256") != sha256_bytes(DIRECT_ENTRY):
        raise ValueError("Prior task report does not identify the exact direct-entry append")
    original_config = baseline.get("original_files", {}).get("/boot/grub/grub.cfg")
    if not original_config:
        raise ValueError("Prior task report has no original GRUB configuration identity")
    old_payloads = baseline.get("new_payload_hashes", {})
    if set(old_payloads) != set(DIRECT_PATHS):
        raise ValueError("Prior task report does not identify exactly the direct-loader payloads")

    require_exclusive_image_access(image)
    identity = disk_identity(image)
    current_dirs, current_files = inventory(image)
    config_path = "/boot/grub/grub.cfg"
    if config_path not in current_files:
        raise ValueError("Installed GRUB configuration is missing")
    config = run_bytes("mtype", "-i", mtools_spec(image), f"::{config_path}")
    if config.count(DIRECT_ENTRY) != 1 or not config.endswith(DIRECT_ENTRY):
        raise ValueError("Published task direct entry is not the exact final config block")
    base_config = config[:-len(DIRECT_ENTRY)]
    if (len(base_config) != original_config["size"]
            or sha256_bytes(base_config) != original_config["sha256"]):
        raise ValueError("Published configuration prefix differs from the original baseline")
    for path in DIRECT_PATHS:
        if current_files.get(path, {}).get("sha256") != old_payloads[path]:
            raise ValueError(f"Published task payload is not the recorded version: {path}")
    task_module = DIRECT_PATHS[0]
    installed_modules = {
        path: record for path, record in current_files.items()
        if path != task_module
    }
    grub_count = check_installed_grub_modules(image, installed_modules)
    for path, record in baseline["original_files"].items():
        if path == config_path:
            continue
        if current_files.get(path) != record:
            raise ValueError(f"Published HDD changed a pre-existing file: {path}")
    expected_current_paths = set(baseline["original_files"]) | set(DIRECT_PATHS)
    if set(current_files) != expected_current_paths:
        raise ValueError("Published HDD file inventory differs from the task-owned baseline")

    module, kernel, hal, new_hashes = read_manifest_inputs(
        kernel_manifest_path, direct_manifest_path
    )
    shutil.copy2(image, candidate)
    try:
        for source, destination in zip((module, kernel, hal), DIRECT_PATHS):
            subprocess.run(
                ["mcopy", "-o", "-i", mtools_spec(candidate), str(source),
                 f"::{destination}"],
                check=True,
            )
        config_temp = work_root / "grub.cfg.cpu-safe.candidate"
        if config_temp.exists() or config_temp.is_symlink():
            raise ValueError(f"Refusing to overwrite staged config: {config_temp}")
        try:
            config_temp.write_bytes(base_config + CPU_SAFE_DIRECT_ENTRY)
            subprocess.run(
                ["mcopy", "-o", "-i", mtools_spec(candidate), str(config_temp),
                 f"::{config_path}"],
                check=True,
            )
        finally:
            config_temp.unlink(missing_ok=True)

        candidate_identity = disk_identity(candidate)
        candidate_dirs, candidate_files = inventory(candidate)
        if candidate_identity != identity:
            raise ValueError("Update changed the MBR, partition, volume ID, image length, or boot area")
        if candidate_dirs != current_dirs or set(candidate_files) != set(current_files):
            raise ValueError("Update changed the existing filesystem path inventory")
        for path, record in current_files.items():
            if path in DIRECT_PATHS or path == config_path:
                continue
            if candidate_files.get(path) != record:
                raise ValueError(f"Update changed an unrelated HDD file: {path}")
        updated_config = run_bytes(
            "mtype", "-i", mtools_spec(candidate), f"::{config_path}"
        )
        if updated_config != base_config + CPU_SAFE_DIRECT_ENTRY:
            raise ValueError("Updated configuration differs from the approved CPU-safe entry")
        for path, expected in zip(DIRECT_PATHS, (
            new_hashes["module"], new_hashes["kernel"], new_hashes["hal"]
        )):
            data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
            if sha256_bytes(data) != expected:
                raise ValueError(f"Updated payload hash mismatch: {path}")

        update_report = {
            "schema_version": 1,
            "update_kind": "cpu-safe-direct-entry",
            "source_image": str(image),
            "source_image_sha256": sha256_file(image),
            "candidate_image": str(candidate),
            "candidate_image_sha256": sha256_file(candidate),
            "baseline_report_sha256": sha256_file(baseline_path),
            "baseline_original_image_sha256": baseline["source_image_sha256"],
            "identity": identity,
            "original_directories": sorted(current_dirs),
            "original_files": current_files,
            "old_payload_hashes": old_payloads,
            "new_payload_hashes": dict(zip(DIRECT_PATHS, (
                new_hashes["module"], new_hashes["kernel"], new_hashes["hal"]
            ))),
            "build_manifests": new_hashes["build_manifests"],
            "original_config_sha256": sha256_bytes(config),
            "updated_config_sha256": sha256_bytes(updated_config),
            "updated_entry_sha256": sha256_bytes(CPU_SAFE_DIRECT_ENTRY),
            "installed_grub_files_compared": grub_count,
            "cpu_safe_menu_id": "undos-direct-cpu-safe",
        }
        report_path.write_text(json.dumps(update_report, indent=2) + "\n")
        print(f"CPU-safe candidate verified: {candidate}")
        print(f"Existing files preserved: {len(current_files) - 1}; "
              f"installed GRUB files matched: {grub_count}")
        print(f"Candidate SHA-256: {update_report['candidate_image_sha256']}")
        print(f"Report: {report_path}")
    except BaseException:
        candidate.unlink(missing_ok=True)
        raise


def prepare_diagnostics(args):
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve()
    report_path = args.report.resolve()
    baseline_path = args.baseline_report.resolve(strict=True)
    current_report_path = args.current_report.resolve(strict=True)
    kernel_manifest_path = args.kernel_manifest.resolve(strict=True)
    direct_manifest_path = args.direct_manifest.resolve(strict=True)
    diagnostic_manifest_path = args.diagnostic_kernel_manifest.resolve(strict=True)
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    backup = work_root / "HDD_IMG.IMG.original.bak"
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Diagnostic update source must be this task's HDD_IMG.IMG")
    if candidate.parent != work_root or candidate.exists() or candidate.is_symlink():
        raise ValueError("Diagnostic candidate must be a new direct child of the HDD update root")
    if report_path.parent != work_root or report_path.exists() or report_path.is_symlink():
        raise ValueError("Diagnostic report must be a new direct child of the HDD update root")
    if baseline_path != (work_root / "candidate-report.json").resolve():
        raise ValueError("Diagnostic update baseline report path is invalid")
    current_report_paths = {
        (work_root / "candidate-cpu-safe-report.json").resolve(),
        (work_root / "candidate-diagnostics-final-report.json").resolve(),
        (work_root / "candidate-diagnostics-f6-verified-report.json").resolve(),
    }
    if current_report_path not in current_report_paths:
        raise ValueError("Current image report path is not an approved task report")

    baseline = json.loads(baseline_path.read_text())
    current_report = json.loads(current_report_path.read_text())
    current_kind = current_report.get("update_kind")
    if current_kind not in ("cpu-safe-direct-entry", "loader-vga-diagnostics"):
        raise ValueError("Current HDD report is not a recorded direct-loader image")
    if current_report.get("candidate_image_sha256") != sha256_file(image):
        raise ValueError("Published HDD differs from the current image report")
    if current_report.get("baseline_report_sha256") != sha256_file(baseline_path):
        raise ValueError("Current image report does not bind the original baseline")
    if current_kind == "cpu-safe-direct-entry":
        if baseline.get("source_image_sha256") != current_report.get(
            "baseline_original_image_sha256"
        ):
            raise ValueError("CPU-safe report does not bind the original image report")
    else:
        cpu_safe_report_path = (work_root / "candidate-cpu-safe-report.json").resolve()
        previous_report_path = (
            work_root / "candidate-diagnostics-final-report.json"
        ).resolve()
        if current_report_path == previous_report_path:
            chain_valid = (
                current_report.get("current_report_sha256")
                == sha256_file(cpu_safe_report_path)
            )
        else:
            chain_valid = (
                current_report.get("current_report_sha256")
                == sha256_file(previous_report_path)
                and current_report.get("source_image_sha256")
                == json.loads(previous_report_path.read_text()).get(
                    "candidate_image_sha256"
                )
                and current_report.get("previous_diagnostic_build_id")
                == json.loads(previous_report_path.read_text()).get(
                    "diagnostic_build_id"
                )
            )
        if (not chain_valid
                or current_report.get("baseline_original_image_sha256")
                != baseline.get("source_image_sha256")):
            raise ValueError("Diagnostic report does not bind the recorded CPU-safe update")
    if not backup.is_file() or backup.is_symlink():
        raise ValueError("Retained original HDD backup is missing or unsafe")
    if sha256_file(backup) != baseline.get("source_image_sha256"):
        raise ValueError("Retained original HDD backup differs from the original baseline")

    require_exclusive_image_access(image)
    identity = disk_identity(image)
    current_dirs, current_files = inventory(image)
    if set(current_files) != set(current_report.get("original_files", {})):
        raise ValueError("Published HDD file inventory differs from the CPU-safe report")
    config_path = "/boot/grub/grub.cfg"
    config = run_bytes("mtype", "-i", mtools_spec(image), f"::{config_path}")
    if current_kind == "cpu-safe-direct-entry":
        current_config_suffix = CPU_SAFE_DIRECT_ENTRY
    else:
        current_build_id = current_report.get("diagnostic_build_id")
        if not isinstance(current_build_id, str) or not re.fullmatch(
            r"[0-9a-f]{8}", current_build_id
        ):
            raise ValueError("Current diagnostic report has an invalid module build ID")
        current_submenu = render_diagnostic_submenu(
            current_build_id, EXPECTED_VOLUME_ID
        ).encode()
        current_config_suffix = CPU_SAFE_DIRECT_ENTRY + current_submenu
    if (
        sha256_bytes(config) != current_report.get("updated_config_sha256")
        or not config.endswith(current_config_suffix)
    ):
        raise ValueError("Published configuration differs from its current image report")
    base_config = config[:-len(current_config_suffix)]
    original_config = baseline.get("original_files", {}).get(config_path)
    if (
        not original_config
        or len(base_config) != original_config.get("size")
        or sha256_bytes(base_config) != original_config.get("sha256")
    ):
        raise ValueError("Configuration prefix differs from its recorded baseline")
    if current_kind == "loader-vga-diagnostics" and (
        sha256_bytes(base_config) != current_report.get("config_prefix_sha256")
    ):
        raise ValueError("Diagnostic configuration prefix differs from its report")
    verify_kernel_stop_pairing(base_config, False)

    old_payloads = current_report.get("new_payload_hashes", {})
    if set(old_payloads) != set(DIRECT_PATHS):
        raise ValueError("CPU-safe report does not identify exactly the direct-loader payloads")
    for path in DIRECT_PATHS:
        if current_files.get(path, {}).get("sha256") != old_payloads[path]:
            raise ValueError(f"Published CPU-safe payload changed: {path}")
    if (DIAGNOSTIC_KERNEL_PATH in current_files
            or DIAGNOSTIC_KERNEL_DIRECTORY in current_dirs):
        raise ValueError("Published HDD already contains a kernel diagnostic payload")
    for path, record in current_report["original_files"].items():
        if path in DIRECT_PATHS or path == config_path:
            continue
        if current_files.get(path) != record:
            raise ValueError(f"Published HDD changed an unrelated file: {path}")

    module, kernel, hal, new_hashes = read_manifest_inputs(
        kernel_manifest_path, direct_manifest_path
    )
    if (new_hashes["kernel"] != old_payloads[DIRECT_PATHS[1]]
            or new_hashes["hal"] != old_payloads[DIRECT_PATHS[2]]):
        raise ValueError(
            "Kernel-entry diagnostic update must preserve the published normal kernel and HAL"
        )
    diagnostic_kernel, diagnostic_record = read_diagnostic_kernel(
        diagnostic_manifest_path
    )
    new_hashes["build_manifests"]["kernel_entry_stop"] = {
        "path": diagnostic_record["manifest_path"],
        "sha256": diagnostic_record["manifest_sha256"],
    }
    new_hashes["kernel_entry_stop"] = diagnostic_record["artifact_sha256"]
    module_path = DIRECT_PATHS[0]
    installed_modules = {
        path: record for path, record in current_files.items()
        if path != module_path
    }
    grub_count = check_installed_grub_modules(image, installed_modules)
    submenu = render_diagnostic_submenu(
        new_hashes["module"][:8],
        "3937-4D14",
        diagnostic_record["artifact_sha256"][:8],
    ).encode()
    updated_config = base_config + CPU_SAFE_DIRECT_ENTRY + submenu
    verify_kernel_stop_pairing(updated_config, True)

    shutil.copy2(image, candidate)
    try:
        for source, destination in zip((module, kernel, hal), DIRECT_PATHS):
            subprocess.run(
                ["mcopy", "-o", "-i", mtools_spec(candidate), str(source),
                 f"::{destination}"],
                check=True,
            )
        subprocess.run(
            ["mmd", "-i", mtools_spec(candidate), f"::{DIAGNOSTIC_KERNEL_DIRECTORY}"],
            check=True,
        )
        subprocess.run(
            ["mcopy", "-o", "-i", mtools_spec(candidate), str(diagnostic_kernel),
             f"::{DIAGNOSTIC_KERNEL_PATH}"],
            check=True,
        )
        config_temp = work_root / "grub.cfg.diagnostics.candidate"
        if config_temp.exists() or config_temp.is_symlink():
            raise ValueError(f"Refusing to overwrite staged config: {config_temp}")
        try:
            config_temp.write_bytes(updated_config)
            subprocess.run(
                ["mcopy", "-o", "-i", mtools_spec(candidate), str(config_temp),
                 f"::{config_path}"],
                check=True,
            )
        finally:
            config_temp.unlink(missing_ok=True)

        if disk_identity(candidate) != identity:
            raise ValueError("Diagnostic update changed the disk identity or boot area")
        candidate_dirs, candidate_files = inventory(candidate)
        if (candidate_dirs != current_dirs | {DIAGNOSTIC_KERNEL_DIRECTORY}
                or set(candidate_files) != set(current_files) | {DIAGNOSTIC_KERNEL_PATH}):
            raise ValueError("Diagnostic update changed the filesystem path inventory")
        for path, record in current_files.items():
            if path in DIRECT_PATHS or path == config_path:
                continue
            if candidate_files.get(path) != record:
                raise ValueError(f"Diagnostic update changed an unrelated HDD file: {path}")
        actual_config = run_bytes(
            "mtype", "-i", mtools_spec(candidate), f"::{config_path}"
        )
        if actual_config != updated_config:
            raise ValueError("Diagnostic menu configuration differs from the expected append")
        for path, expected in zip(DIRECT_PATHS, (
            new_hashes["module"], new_hashes["kernel"], new_hashes["hal"]
        )):
            data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
            if sha256_bytes(data) != expected:
                raise ValueError(f"Diagnostic update payload hash mismatch: {path}")
        diagnostic_data = run_bytes(
            "mtype", "-i", mtools_spec(candidate), f"::{DIAGNOSTIC_KERNEL_PATH}"
        )
        if sha256_bytes(diagnostic_data) != new_hashes["kernel_entry_stop"]:
            raise ValueError("Kernel diagnostic update payload hash mismatch")

        report = {
            "schema_version": 1,
            "update_kind": "kernel-entry-stop-diagnostics",
            "source_image": str(image),
            "source_image_sha256": sha256_file(image),
            "candidate_image": str(candidate),
            "candidate_image_sha256": sha256_file(candidate),
            "baseline_report_sha256": sha256_file(baseline_path),
            "current_report_sha256": sha256_file(current_report_path),
            "baseline_original_image_sha256": baseline["source_image_sha256"],
            "identity": identity,
            "original_directories": sorted(current_dirs),
            "original_files": current_files,
            "old_payload_hashes": old_payloads,
            "new_payload_hashes": {
                **dict(zip(DIRECT_PATHS, (
                    new_hashes["module"], new_hashes["kernel"], new_hashes["hal"]
                ))),
                DIAGNOSTIC_KERNEL_PATH: new_hashes["kernel_entry_stop"],
            },
            "build_manifests": new_hashes["build_manifests"],
            "original_config_sha256": sha256_bytes(config),
            "config_prefix_sha256": sha256_bytes(base_config),
            "updated_config_sha256": sha256_bytes(updated_config),
            "diagnostic_submenu_sha256": sha256_bytes(submenu),
            "diagnostic_build_id": new_hashes["module"][:8],
            "kernel_entry_stop_build_id": new_hashes["kernel_entry_stop"][:8],
            "kernel_entry_stop_manifest": diagnostic_record,
            "source_report_kind": current_kind,
            "previous_diagnostic_build_id": (
                current_report.get("diagnostic_build_id")
                if current_kind == "loader-vga-diagnostics" else None
            ),
            "installed_grub_files_compared": grub_count,
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Diagnostic HDD candidate verified: {candidate}")
        print(f"Existing files preserved: {len(current_files) - 1}; "
              f"installed GRUB files matched: {grub_count}")
        print(f"Diagnostic module build ID: {report['diagnostic_build_id']}")
        print(
            "Kernel-entry diagnostic build ID: "
            f"{report['kernel_entry_stop_build_id']}"
        )
        print(f"Candidate SHA-256: {report['candidate_image_sha256']}")
        print(f"Report: {report_path}")
    except BaseException:
        candidate.unlink(missing_ok=True)
        raise


def publish(args):
    if args.image.is_symlink():
        raise ValueError("Publication target path must not be a symlink")
    if args.boot_image.is_symlink() or args.boot_log.is_symlink():
        raise ValueError("Boot evidence paths must not be symlinks")
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    boot_image = args.boot_image.resolve(strict=True)
    report_path = args.report.resolve(strict=True)
    boot_log = args.boot_log.resolve(strict=True)
    backup = args.backup.resolve()
    report = json.loads(report_path.read_text())
    verify_recorded_manifests(report)
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Publication target must be this task's HDD_IMG.IMG")
    if candidate != Path(report["candidate_image"]).resolve():
        raise ValueError("Candidate path does not match the verified report")
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    if boot_image in (image, candidate) or work_root not in boot_image.parents:
        raise ValueError("Boot evidence must come from a separate task-local image copy")
    if report.get("source_image_sha256") != sha256_file(image):
        raise ValueError("Original HDD changed after candidate preparation")
    if report.get("candidate_image_sha256") != sha256_file(candidate):
        raise ValueError("Candidate changed after verification")
    if report.get("candidate_image_sha256") != sha256_file(boot_image):
        raise ValueError("Boot-tested image is not an exact copy of the candidate")
    require_exclusive_image_access(image)
    require_exclusive_image_access(candidate)
    require_exclusive_image_access(boot_image)
    log_text = boot_log.read_text(errors="replace")
    missing = [milestone for milestone in REQUIRED_BOOT_MILESTONES
               if milestone not in log_text]
    if missing:
        raise ValueError(f"Candidate boot log is missing milestones: {missing}")
    identity = disk_identity(image)
    if identity != report["identity"] or disk_identity(candidate) != identity:
        raise ValueError("Original or candidate disk identity changed")
    candidate_dirs, candidate_files = inventory(candidate)
    if not set(report["original_directories"]).issubset(candidate_dirs):
        raise ValueError("Candidate removed an existing filesystem directory")
    config_path = "/boot/grub/grub.cfg"
    for path, record in report["original_files"].items():
        if path == config_path:
            continue
        if candidate_files.get(path) != record:
            raise ValueError(f"Candidate changed or removed existing file: {path}")
    config = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{config_path}")
    original_config = report["original_files"][config_path]
    if (len(config) != original_config["size"] + len(DIRECT_ENTRY)
            or not config.endswith(DIRECT_ENTRY)
            or sha256_bytes(config[:-len(DIRECT_ENTRY)]) != original_config["sha256"]):
        raise ValueError("Candidate configuration is not the verified append-only update")
    for path, expected in report["new_payload_hashes"].items():
        data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
        if sha256_bytes(data) != expected:
            raise ValueError(f"Candidate payload changed: {path}")
    if backup.exists() or backup.is_symlink():
        raise ValueError(f"Backup path already exists: {backup}")
    if backup.parent != work_root:
        raise ValueError("Backup must remain in the task-local HDD update build root")
    if len({image.stat().st_dev, candidate.stat().st_dev, backup.parent.stat().st_dev}) != 1:
        raise ValueError("Original, candidate, and backup must share one filesystem")
    if sha256_file(image) != report["source_image_sha256"]:
        raise ValueError("Original HDD changed before backup creation")
    shutil.copy2(image, backup)
    with backup.open("rb") as saved:
        os.fsync(saved.fileno())
    if sha256_file(backup) != report["source_image_sha256"]:
        raise ValueError("Retained original backup failed its hash check")
    if sha256_file(image) != report["source_image_sha256"]:
        raise ValueError("Original HDD changed during backup creation")
    os.replace(candidate, image)
    directory_fd = os.open(image.parent, os.O_RDONLY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)
    if sha256_file(image) != report["candidate_image_sha256"]:
        raise ValueError(
            f"Published image hash mismatch; original backup retained at {backup}"
        )
    print(f"Published private HDD image: {image}")
    print(f"Published SHA-256: {sha256_file(image)}")
    print(f"Original backup retained: {backup}")


def publish_update(args):
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    boot_image = args.boot_image.resolve(strict=True)
    report_path = args.report.resolve(strict=True)
    boot_log = args.boot_log.resolve(strict=True)
    baseline_path = args.baseline_report.resolve(strict=True)
    backup = args.backup.resolve(strict=True)
    snapshot = args.snapshot.resolve()
    report = json.loads(report_path.read_text())
    baseline = json.loads(baseline_path.read_text())
    verify_recorded_manifests(report)
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Publication target must be this task's HDD_IMG.IMG")
    if report.get("update_kind") != "cpu-safe-direct-entry":
        raise ValueError("Candidate report is not a CPU-safe direct-entry update")
    if candidate != Path(report["candidate_image"]).resolve():
        raise ValueError("Candidate path does not match the verified update report")
    if boot_image in (image, candidate) or work_root not in boot_image.parents:
        raise ValueError("Boot evidence must come from a separate task-local image copy")
    if baseline_path != (work_root / "candidate-report.json").resolve():
        raise ValueError("Original task baseline report path is invalid")
    if report.get("baseline_report_sha256") != sha256_file(baseline_path):
        raise ValueError("Original task baseline report changed after candidate preparation")
    if report.get("source_image_sha256") != sha256_file(image):
        raise ValueError("Published HDD changed after update candidate preparation")
    if report.get("candidate_image_sha256") != sha256_file(candidate):
        raise ValueError("CPU-safe candidate changed after verification")
    if report.get("candidate_image_sha256") != sha256_file(boot_image):
        raise ValueError("Boot-tested image is not an exact copy of the CPU-safe candidate")
    if backup != (work_root / "HDD_IMG.IMG.original.bak").resolve():
        raise ValueError("Original backup path does not match the retained task backup")
    if snapshot.parent != work_root or snapshot.exists() or snapshot.is_symlink():
        raise ValueError("Current-image snapshot must be a new direct child of the HDD update build root")
    if report.get("baseline_original_image_sha256") != baseline.get("source_image_sha256"):
        raise ValueError("Update report does not preserve the original-image baseline")
    if sha256_file(backup) != baseline["source_image_sha256"]:
        raise ValueError("Retained original backup does not match the original task baseline")
    require_exclusive_image_access(image)
    require_exclusive_image_access(candidate)
    require_exclusive_image_access(boot_image)
    log_text = boot_log.read_text(errors="replace")
    missing = [milestone for milestone in REQUIRED_BOOT_MILESTONES
               if milestone not in log_text]
    if missing:
        raise ValueError(f"CPU-safe candidate boot log is missing milestones: {missing}")
    identity = disk_identity(image)
    if identity != report["identity"] or disk_identity(candidate) != identity:
        raise ValueError("Published HDD or candidate disk identity changed")

    candidate_dirs, candidate_files = inventory(candidate)
    if candidate_dirs != set(report["original_directories"]):
        raise ValueError("CPU-safe candidate changed the filesystem directory inventory")
    if set(candidate_files) != set(report["original_files"]):
        raise ValueError("CPU-safe candidate changed the filesystem file inventory")
    config_path = "/boot/grub/grub.cfg"
    for path, record in report["original_files"].items():
        if path == config_path or path in DIRECT_PATHS:
            continue
        if candidate_files.get(path) != record:
            raise ValueError(f"CPU-safe candidate changed an unrelated HDD file: {path}")
    config = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{config_path}")
    if (sha256_bytes(config) != report["updated_config_sha256"]
            or not config.endswith(CPU_SAFE_DIRECT_ENTRY)
            or CPU_SAFE_DIRECT_ENTRY.count(b"undos-direct-cpu-safe") != 1):
        raise ValueError("CPU-safe candidate menu entry differs from the verified update")
    for path, expected in report["new_payload_hashes"].items():
        data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
        if sha256_bytes(data) != expected:
            raise ValueError(f"CPU-safe candidate payload changed: {path}")
    if len({image.stat().st_dev, candidate.stat().st_dev, boot_image.stat().st_dev,
            backup.stat().st_dev, snapshot.parent.stat().st_dev}) != 1:
        raise ValueError("Images, backup, and snapshot must share one filesystem")
    if shutil.disk_usage(work_root).free < image.stat().st_size:
        raise ValueError("Insufficient space to snapshot the current published HDD")
    if sha256_file(image) != report["source_image_sha256"]:
        raise ValueError("Published HDD changed before snapshot creation")

    snapshot_temporary = work_root / f".{snapshot.name}.copying"
    if snapshot_temporary.exists() or snapshot_temporary.is_symlink():
        raise ValueError(f"Refusing to overwrite snapshot staging path: {snapshot_temporary}")
    try:
        shutil.copy2(image, snapshot_temporary)
        with snapshot_temporary.open("rb") as saved:
            os.fsync(saved.fileno())
        if sha256_file(snapshot_temporary) != report["source_image_sha256"]:
            raise ValueError("Current-image snapshot failed its hash check")
        if sha256_file(image) != report["source_image_sha256"]:
            raise ValueError("Published HDD changed while creating its snapshot")
        os.replace(snapshot_temporary, snapshot)
        directory_fd = os.open(snapshot.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except BaseException:
        snapshot_temporary.unlink(missing_ok=True)
        raise

    os.replace(candidate, image)
    directory_fd = os.open(image.parent, os.O_RDONLY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)
    if sha256_file(image) != report["candidate_image_sha256"]:
        raise ValueError(
            f"Published image hash mismatch; immediate snapshot retained at {snapshot}"
        )
    print(f"Published CPU-safe diagnostic HDD entry: {image}")
    print(f"Published SHA-256: {sha256_file(image)}")
    print(f"Original backup retained: {backup}")
    print(f"Pre-update snapshot retained: {snapshot}")


def publish_diagnostics(args):
    if args.image.is_symlink():
        raise ValueError("Publication target path must not be a symlink")
    image = args.image.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    boot_image = args.boot_image.resolve(strict=True)
    report_path = args.report.resolve(strict=True)
    boot_log = args.boot_log.resolve(strict=True)
    baseline_path = args.baseline_report.resolve(strict=True)
    current_report_path = args.current_report.resolve(strict=True)
    backup = args.backup.resolve(strict=True)
    snapshot = args.snapshot.resolve()
    report = json.loads(report_path.read_text())
    baseline = json.loads(baseline_path.read_text())
    current_report = json.loads(current_report_path.read_text())
    verify_recorded_manifests(report)
    work_root = (ROOT / "build/grub-direct-loader/hdd-update").resolve()
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Publication target must be this task's HDD_IMG.IMG")
    report_kind = report.get("update_kind")
    if report_kind not in (
        "loader-vga-diagnostics", "kernel-entry-stop-diagnostics"
    ):
        raise ValueError("Candidate report is not a loader diagnostic update")
    if candidate != Path(report["candidate_image"]).resolve():
        raise ValueError("Candidate path does not match the diagnostic report")
    if boot_image in (image, candidate) or work_root not in boot_image.parents:
        raise ValueError("Boot evidence must come from a separate task-local image copy")
    if baseline_path != (work_root / "candidate-report.json").resolve():
        raise ValueError("Original task baseline report path is invalid")
    current_report_paths = {
        (work_root / "candidate-cpu-safe-report.json").resolve(),
        (work_root / "candidate-diagnostics-final-report.json").resolve(),
        (work_root / "candidate-diagnostics-f6-verified-report.json").resolve(),
    }
    if current_report_path not in current_report_paths:
        raise ValueError("Current image report path is not an approved task report")
    current_kind = current_report.get("update_kind")
    if current_kind not in ("cpu-safe-direct-entry", "loader-vga-diagnostics"):
        raise ValueError("Current report does not describe a supported direct-loader image")
    if report.get("baseline_report_sha256") != sha256_file(baseline_path):
        raise ValueError("Original task baseline report changed after candidate preparation")
    if report.get("current_report_sha256") != sha256_file(current_report_path):
        raise ValueError("Current image report changed after diagnostic preparation")
    if report.get("source_image_sha256") != sha256_file(image):
        raise ValueError("Published HDD changed after diagnostic preparation")
    if report.get("candidate_image_sha256") != sha256_file(candidate):
        raise ValueError("Diagnostic candidate changed after verification")
    if report.get("candidate_image_sha256") != sha256_file(boot_image):
        raise ValueError("Boot-tested image is not an exact copy of the diagnostic candidate")
    if backup != (work_root / "HDD_IMG.IMG.original.bak").resolve():
        raise ValueError("Original backup path does not match the retained task backup")
    expected_snapshot_name = {
        "cpu-safe-direct-entry": "HDD_IMG.IMG.pre-diagnostics.bak",
        "loader-vga-diagnostics": "HDD_IMG.IMG.pre-f6-correction.bak",
    }.get(current_kind)
    if report_kind == "kernel-entry-stop-diagnostics":
        expected_snapshot_name = "HDD_IMG.IMG.pre-kernel-entry-stop.bak"
    if snapshot != (work_root / expected_snapshot_name).resolve():
        raise ValueError("Immediate diagnostic snapshot path is invalid for this update")
    if snapshot.exists() or snapshot.is_symlink():
        raise ValueError("Immediate diagnostic snapshot already exists")
    if report.get("baseline_original_image_sha256") != baseline.get("source_image_sha256"):
        raise ValueError("Diagnostic report does not preserve the original-image baseline")
    if sha256_file(backup) != baseline.get("source_image_sha256"):
        raise ValueError("Retained original backup differs from the original baseline")
    if current_report.get("candidate_image_sha256") != report.get("source_image_sha256"):
        raise ValueError("Diagnostic update is not based on the verified current HDD")
    if current_report.get("baseline_report_sha256") != sha256_file(baseline_path):
        raise ValueError("Current image report does not bind the original task baseline")
    if current_report.get("baseline_original_image_sha256") != baseline.get(
        "source_image_sha256"
    ):
        raise ValueError("Current image report does not bind the original HDD identity")
    if current_kind == "loader-vga-diagnostics":
        if report.get("previous_diagnostic_build_id") != current_report.get(
            "diagnostic_build_id"
        ):
            raise ValueError("Corrected diagnostic report does not follow the current menu build")
        if report.get("source_report_kind") != current_kind:
            raise ValueError("Corrected diagnostic report has an inconsistent source kind")
        if report.get("source_image_sha256") != current_report.get(
            "candidate_image_sha256"
        ):
            raise ValueError("Corrected candidate source image differs from the current report")
    elif report.get("source_report_kind") not in (None, current_kind):
        raise ValueError("Diagnostic report has an inconsistent source image kind")

    require_exclusive_image_access(image)
    require_exclusive_image_access(candidate)
    require_exclusive_image_access(boot_image)
    log_text = boot_log.read_text(errors="replace")
    missing = [milestone for milestone in REQUIRED_BOOT_MILESTONES
               if milestone not in log_text]
    if missing:
        raise ValueError(f"Diagnostic candidate normal boot missed milestones: {missing}")
    identity = disk_identity(image)
    if identity != report["identity"] or disk_identity(candidate) != identity:
        raise ValueError("Published HDD or candidate disk identity changed")

    candidate_dirs, candidate_files = inventory(candidate)
    expected_directories = set(report["original_directories"])
    expected_files = set(report["original_files"])
    if report_kind == "kernel-entry-stop-diagnostics":
        expected_directories.add(DIAGNOSTIC_KERNEL_DIRECTORY)
        expected_files.add(DIAGNOSTIC_KERNEL_PATH)
    if candidate_dirs != expected_directories:
        raise ValueError("Diagnostic candidate changed the filesystem directory inventory")
    if set(candidate_files) != expected_files:
        raise ValueError("Diagnostic candidate changed the filesystem file inventory")
    config_path = "/boot/grub/grub.cfg"
    for path, record in report["original_files"].items():
        if (path == config_path or path in DIRECT_PATHS
                or path == DIAGNOSTIC_KERNEL_PATH):
            continue
        if candidate_files.get(path) != record:
            raise ValueError(f"Diagnostic candidate changed an unrelated HDD file: {path}")
    current_dirs, current_files = inventory(image)
    if set(current_files) != set(current_report.get("original_files", {})):
        raise ValueError("Published HDD inventory differs from the current image report")
    for path, record in current_report["original_files"].items():
        if path in DIRECT_PATHS or path == config_path:
            continue
        if current_files.get(path) != record:
            raise ValueError(f"Published HDD changed since its current report: {path}")
    for path, expected in current_report.get("new_payload_hashes", {}).items():
        if current_files.get(path, {}).get("sha256") != expected:
            raise ValueError(f"Published payload differs from the current image report: {path}")
    current_config = run_bytes(
        "mtype", "-i", mtools_spec(image), f"::{config_path}"
    )
    if sha256_bytes(current_config) != current_report.get("updated_config_sha256"):
        raise ValueError("Published GRUB configuration differs from the current image report")
    config = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{config_path}")
    kernel_build_id = (
        report.get("kernel_entry_stop_build_id")
        if report_kind == "kernel-entry-stop-diagnostics" else None
    )
    submenu = render_diagnostic_submenu(
        report["diagnostic_build_id"], "3937-4D14", kernel_build_id
    ).encode()
    verify_kernel_stop_pairing(
        config, report_kind == "kernel-entry-stop-diagnostics"
    )
    if not config.endswith(CPU_SAFE_DIRECT_ENTRY + submenu):
        raise ValueError("Diagnostic candidate does not retain the CPU-safe entry and submenu")
    config_prefix = config[:-len(CPU_SAFE_DIRECT_ENTRY + submenu)]
    expected_config = config_prefix + CPU_SAFE_DIRECT_ENTRY + submenu
    if (
        sha256_bytes(config_prefix) != report.get("config_prefix_sha256")
        or sha256_bytes(config) != report.get("updated_config_sha256")
        or sha256_bytes(submenu) != report.get("diagnostic_submenu_sha256")
        or config != expected_config
    ):
        raise ValueError("Diagnostic candidate menu differs from its verified report")
    for path, expected in report["new_payload_hashes"].items():
        data = run_bytes("mtype", "-i", mtools_spec(candidate), f"::{path}")
        if sha256_bytes(data) != expected:
            raise ValueError(f"Diagnostic candidate payload changed: {path}")
    if len({
        image.stat().st_dev, candidate.stat().st_dev, boot_image.stat().st_dev,
        backup.stat().st_dev, snapshot.parent.stat().st_dev,
    }) != 1:
        raise ValueError("Images, backup, and snapshot must share one filesystem")
    if shutil.disk_usage(work_root).free < image.stat().st_size:
        raise ValueError("Insufficient space to snapshot the current published HDD")
    if sha256_file(image) != report["source_image_sha256"]:
        raise ValueError("Published HDD changed before snapshot creation")

    snapshot_temporary = work_root / f".{snapshot.name}.copying"
    if snapshot_temporary.exists() or snapshot_temporary.is_symlink():
        raise ValueError(f"Refusing to overwrite snapshot staging path: {snapshot_temporary}")
    try:
        shutil.copy2(image, snapshot_temporary)
        with snapshot_temporary.open("rb") as saved:
            os.fsync(saved.fileno())
        if sha256_file(snapshot_temporary) != report["source_image_sha256"]:
            raise ValueError("Immediate diagnostic snapshot failed its hash check")
        if sha256_file(image) != report["source_image_sha256"]:
            raise ValueError("Published HDD changed while creating its snapshot")
        os.replace(snapshot_temporary, snapshot)
        directory_fd = os.open(snapshot.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except BaseException:
        snapshot_temporary.unlink(missing_ok=True)
        raise

    os.replace(candidate, image)
    directory_fd = os.open(image.parent, os.O_RDONLY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)
    if sha256_file(image) != report["candidate_image_sha256"]:
        raise ValueError(
            f"Published image hash mismatch; immediate snapshot retained at {snapshot}"
        )
    print(f"Published private VGA diagnostic HDD menu: {image}")
    print(f"Published SHA-256: {sha256_file(image)}")
    print(f"Original backup retained: {backup}")
    print(f"Immediate pre-update snapshot retained: {snapshot}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare_parser = commands.add_parser("prepare")
    prepare_parser.add_argument("--image", type=Path, required=True)
    prepare_parser.add_argument("--candidate", type=Path, required=True)
    prepare_parser.add_argument("--kernel-manifest", type=Path, required=True)
    prepare_parser.add_argument("--direct-manifest", type=Path, required=True)
    prepare_parser.set_defaults(func=prepare)
    publish_parser = commands.add_parser("publish")
    publish_parser.add_argument("--image", type=Path, required=True)
    publish_parser.add_argument("--candidate", type=Path, required=True)
    publish_parser.add_argument("--boot-image", type=Path, required=True)
    publish_parser.add_argument("--report", type=Path, required=True)
    publish_parser.add_argument("--boot-log", type=Path, required=True)
    publish_parser.add_argument("--backup", type=Path, required=True)
    publish_parser.set_defaults(func=publish)
    update_parser = commands.add_parser("prepare-update")
    update_parser.add_argument("--image", type=Path, required=True)
    update_parser.add_argument("--candidate", type=Path, required=True)
    update_parser.add_argument("--report", type=Path, required=True)
    update_parser.add_argument("--baseline-report", type=Path, required=True)
    update_parser.add_argument("--kernel-manifest", type=Path, required=True)
    update_parser.add_argument("--direct-manifest", type=Path, required=True)
    update_parser.set_defaults(func=prepare_update)
    publish_update_parser = commands.add_parser("publish-update")
    publish_update_parser.add_argument("--image", type=Path, required=True)
    publish_update_parser.add_argument("--candidate", type=Path, required=True)
    publish_update_parser.add_argument("--boot-image", type=Path, required=True)
    publish_update_parser.add_argument("--report", type=Path, required=True)
    publish_update_parser.add_argument("--boot-log", type=Path, required=True)
    publish_update_parser.add_argument("--baseline-report", type=Path, required=True)
    publish_update_parser.add_argument("--backup", type=Path, required=True)
    publish_update_parser.add_argument("--snapshot", type=Path, required=True)
    publish_update_parser.set_defaults(func=publish_update)
    diagnostic_parser = commands.add_parser("prepare-diagnostics")
    diagnostic_parser.add_argument("--image", type=Path, required=True)
    diagnostic_parser.add_argument("--candidate", type=Path, required=True)
    diagnostic_parser.add_argument("--report", type=Path, required=True)
    diagnostic_parser.add_argument("--baseline-report", type=Path, required=True)
    diagnostic_parser.add_argument("--current-report", type=Path, required=True)
    diagnostic_parser.add_argument("--kernel-manifest", type=Path, required=True)
    diagnostic_parser.add_argument("--direct-manifest", type=Path, required=True)
    diagnostic_parser.add_argument(
        "--diagnostic-kernel-manifest", type=Path, required=True
    )
    diagnostic_parser.set_defaults(func=prepare_diagnostics)
    publish_diagnostic_parser = commands.add_parser("publish-diagnostics")
    publish_diagnostic_parser.add_argument("--image", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--candidate", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--boot-image", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--report", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--boot-log", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--baseline-report", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--current-report", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--backup", type=Path, required=True)
    publish_diagnostic_parser.add_argument("--snapshot", type=Path, required=True)
    publish_diagnostic_parser.set_defaults(func=publish_diagnostics)
    args = parser.parse_args()
    work_root = ROOT / "build/grub-direct-loader/hdd-update"
    work_root.mkdir(parents=True, exist_ok=True)
    with (work_root / "update.lock").open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        args.func(args)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
