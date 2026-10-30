#!/usr/bin/env python3
"""Exercise the direct-module target BootInfo probe publication gate."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def copy_recorded_inputs(project, evidence, evidence_path, kernel_manifest,
                         direct_manifest, apps_manifest):
    files = set(evidence["dependencies"])
    files.update((
        evidence["object"]["path"],
        evidence["fixture"]["path"],
        evidence["target_flags"]["path"],
        evidence["depfile"]["path"],
        evidence_path.relative_to(ROOT).as_posix(),
        kernel_manifest.relative_to(ROOT).as_posix(),
        direct_manifest.relative_to(ROOT).as_posix(),
        apps_manifest.relative_to(ROOT).as_posix(),
        "Tools/build-grub-direct.py",
        "Tools/verify-bootinfo-layout.py",
        "Image/assemble.py",
    ))
    for relative in sorted(files):
        source = ROOT / relative
        destination = project / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def run_gate(project, evidence_path, kernel_manifest, output_manifest):
    command = [
        sys.executable,
        str(project / "Tools/build-grub-direct.py"),
        "--kernel-manifest", str(project / kernel_manifest),
        "--bootinfo-probe-manifest", str(project / evidence_path),
        "--output-manifest", str(project / output_manifest),
        "--check-probe",
    ]
    return subprocess.run(command, cwd=project, text=True, capture_output=True)


def run_image(project, kernel_manifest, direct_manifest, apps_manifest, output):
    command = [
        sys.executable, str(project / "Image/assemble.py"),
        "--kernel-manifest", str(project / kernel_manifest),
        "--direct-loader-manifest", str(project / direct_manifest),
        "--apps-manifest", str(project / apps_manifest),
        "--output", str(project / output),
    ]
    return subprocess.run(command, cwd=project, text=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe-manifest", type=Path, required=True)
    parser.add_argument("--kernel-manifest", type=Path, required=True)
    parser.add_argument("--direct-manifest", type=Path, required=True)
    parser.add_argument("--apps-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    evidence_path = args.probe_manifest.resolve()
    kernel_manifest = args.kernel_manifest.resolve()
    direct_manifest = args.direct_manifest.resolve()
    apps_manifest = args.apps_manifest.resolve()
    evidence = json.loads(evidence_path.read_text())
    output = args.output.resolve()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    missing_project = output / "missing-evidence"
    missing_project.mkdir()
    copy_recorded_inputs(
        missing_project, evidence, evidence_path, kernel_manifest,
        direct_manifest, apps_manifest
    )
    relative_evidence = evidence_path.relative_to(ROOT)
    relative_kernel = kernel_manifest.relative_to(ROOT)
    relative_direct = direct_manifest.relative_to(ROOT)
    relative_apps = apps_manifest.relative_to(ROOT)
    valid_project = output / "valid-evidence"
    valid_project.mkdir()
    copy_recorded_inputs(
        valid_project, evidence, evidence_path, kernel_manifest,
        direct_manifest, apps_manifest
    )
    valid_result = run_gate(
        valid_project, relative_evidence, relative_kernel,
        Path("build/direct-manifest.json"),
    )
    if valid_result.returncode != 0:
        raise RuntimeError(
            "Current probe evidence was rejected: "
            + (valid_result.stderr.strip() or valid_result.stdout.strip())
        )
    if (valid_project / "build/direct-manifest.json").exists():
        raise RuntimeError("Probe-only verification published a module manifest")

    missing_result = run_gate(
        missing_project, relative_evidence.with_name("missing-probe.json"),
        relative_kernel, Path("build/direct-manifest.json"),
    )
    if (missing_result.returncode == 0
            or "BootInfo target probe evidence rejected" not in missing_result.stderr
            or "is missing" not in missing_result.stderr):
        raise RuntimeError("Module verification accepted missing probe evidence")
    if (missing_project / "build/direct-manifest.json").exists():
        raise RuntimeError("Missing-probe case published a module manifest")

    stale_project = output / "changed-layout-input"
    stale_project.mkdir()
    copy_recorded_inputs(
        stale_project, evidence, evidence_path, kernel_manifest,
        direct_manifest, apps_manifest
    )
    changed_input = stale_project / "Kernel/API/include/kernel/boot/boot_info.hpp"
    changed_input.write_bytes(changed_input.read_bytes() + b"\n")
    stale_result = run_gate(
        stale_project, relative_evidence, relative_kernel,
        Path("build/direct-manifest.json"),
    )
    if (stale_result.returncode == 0
            or "BootInfo layout input is missing or stale" not in stale_result.stderr):
        raise RuntimeError("Module verification accepted a changed layout input")
    if (stale_project / "build/direct-manifest.json").exists():
        raise RuntimeError("Changed-layout case published a module manifest")

    image_output = Path("build/protected-image")
    protected = stale_project / image_output / "preserve-me"
    protected.parent.mkdir(parents=True, exist_ok=True)
    protected.write_text("staging must survive stale provenance rejection\n")
    image_result = run_image(
        stale_project, relative_kernel, relative_direct, relative_apps,
        image_output,
    )
    if (image_result.returncode == 0
            or "BootInfo layout input is missing or stale"
            not in image_result.stderr):
        raise RuntimeError("Image assembly accepted a changed layout input")
    if not protected.is_file():
        raise RuntimeError("Image assembly removed prior staging before rejecting stale evidence")

    print("current probe evidence accepted by the module-build gate")
    print("missing probe evidence rejected before module-manifest publication")
    print(missing_result.stderr.strip() or missing_result.stdout.strip())
    print("changed BootInfo layout input rejected before module-manifest publication")
    print(stale_result.stderr.strip() or stale_result.stdout.strip())
    print("image assembly rejected stale layout input and preserved existing staging")
    print(image_result.stderr.strip() or image_result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
