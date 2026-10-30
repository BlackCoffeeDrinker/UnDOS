#!/usr/bin/env python3
"""Verify the target-compiled BootInfo fixture bytes against the wire layout."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

EXPECTED_SIZE = 59060
COMMAND_LINE_OFFSET = 58032
COMMAND_LINE = b"CMDLINE=12\0"
PROBE_SOURCE = "Boot/GrubDirect/bootinfo_layout_probe.cpp"
REQUIRED_LAYOUT_INPUTS = {
    PROBE_SOURCE,
    "Boot/GrubDirect/bootinfo_wire.h",
    "Boot/GrubDirect/core/types.h",
    "Kernel/API/include/kernel/boot/boot_info.hpp",
    "SDK/KStd/include/static_string.hpp",
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_project_path(root, value):
    path = Path(value)
    if path.is_absolute() and path.parts[:2] == ("/", "src"):
        path = root.joinpath(*path.parts[2:])
    elif not path.is_absolute():
        path = root / path
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise ValueError(f"BootInfo probe path escapes the project: {value}") from error
    return path


def project_relative(root, path):
    path = resolve_project_path(root, str(path))
    return path.resolve().relative_to(root.resolve()).as_posix()


def dependency_paths(root, depfile):
    content = depfile.read_text().replace("\\\n", " ")
    if ":" not in content:
        raise ValueError(f"BootInfo probe dependency file is malformed: {depfile}")
    dependencies = shlex.split(content.split(":", 1)[1])
    return sorted({
        project_relative(root, dependency)
        for dependency in dependencies
        if (Path(dependency).is_absolute()
            and (Path(dependency).parts[:2] == ("/", "src")
                 or Path(dependency).is_relative_to(root)))
    })


def fixture_oracle():
    expected = bytearray(EXPECTED_SIZE)
    expected[0:4] = (0x12345678).to_bytes(4, "little")
    expected[COMMAND_LINE_OFFSET:COMMAND_LINE_OFFSET + 4] = (
        (len(COMMAND_LINE) - 1).to_bytes(4, "little")
    )
    start = COMMAND_LINE_OFFSET + 4
    expected[start:start + len(COMMAND_LINE)] = COMMAND_LINE
    return bytes(expected)


def verify_fixture_bytes(fixture):
    actual = fixture.read_bytes()
    expected = fixture_oracle()
    if actual != expected:
        mismatch = next(
            (index for index, (left, right) in enumerate(zip(actual, expected))
             if left != right),
            min(len(actual), len(expected)),
        )
        raise ValueError(
            f"BootInfo fixture mismatch at byte {mismatch}: "
            f"actual-size={len(actual)} expected-size={len(expected)}"
        )
    return len(actual)


def create_evidence(root, object_path, fixture_path, depfile_path, flags_path,
                    kernel_manifest_path):
    root = root.resolve()
    object_path = resolve_project_path(root, str(object_path))
    fixture_path = resolve_project_path(root, str(fixture_path))
    depfile_path = resolve_project_path(root, str(depfile_path))
    flags_path = resolve_project_path(root, str(flags_path))
    kernel_manifest_path = resolve_project_path(root, str(kernel_manifest_path))
    if not object_path.is_file():
        raise ValueError(f"target probe object is missing: {object_path}")
    if object_path.read_bytes()[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("BootInfo probe object is not ELF32 little-endian")
    fixture_size = verify_fixture_bytes(fixture_path)
    dependencies = dependency_paths(root, depfile_path)
    if not REQUIRED_LAYOUT_INPUTS.issubset(dependencies):
        missing = sorted(REQUIRED_LAYOUT_INPUTS - set(dependencies))
        raise ValueError(f"BootInfo probe omitted layout inputs: {missing}")
    flags_text = flags_path.read_text()
    flags_match = re.search(r"^CXX_FLAGS = (.*)$", flags_text, re.MULTILINE)
    includes_match = re.search(r"^CXX_INCLUDES = (.*)$", flags_text, re.MULTILINE)
    if not flags_match or not includes_match:
        raise ValueError("BootInfo probe target flags are incomplete")
    target_flags = shlex.split(flags_match.group(1))
    if "-m32" not in target_flags or "-march=i386" not in target_flags:
        raise ValueError("BootInfo probe is not compiled for the i386 target")
    if not kernel_manifest_path.is_file():
        raise ValueError(f"kernel artifact manifest is missing: {kernel_manifest_path}")
    return {
        "schema_version": 1,
        "verified": True,
        "target": "i386",
        "fixture_size": fixture_size,
        "kernel_manifest": {
            "path": project_relative(root, kernel_manifest_path),
            "sha256": sha256(kernel_manifest_path),
        },
        "probe_source": {
            "path": PROBE_SOURCE,
            "sha256": sha256(resolve_project_path(root, PROBE_SOURCE)),
        },
        "target_flags": {
            "path": project_relative(root, flags_path),
            "sha256": sha256(flags_path),
            "cxx_flags": flags_match.group(1),
            "include_flags": includes_match.group(1),
        },
        "dependencies": {
            path: sha256(resolve_project_path(root, path))
            for path in dependencies
        },
        "object": {
            "path": project_relative(root, object_path),
            "sha256": sha256(object_path),
        },
        "fixture": {
            "path": project_relative(root, fixture_path),
            "sha256": sha256(fixture_path),
        },
        "depfile": {
            "path": project_relative(root, depfile_path),
            "sha256": sha256(depfile_path),
        },
    }


def verify_evidence(manifest_path, kernel_manifest_path, root, reextract_fixture=None,
                    objcopy=None):
    root = root.resolve()
    if not manifest_path.is_file():
        raise ValueError(f"BootInfo probe evidence is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema_version") != 1 or manifest.get("verified") is not True:
        raise ValueError("BootInfo probe evidence is not verified")
    if manifest.get("target") != "i386":
        raise ValueError("BootInfo probe evidence is not for i386")
    kernel_record = manifest.get("kernel_manifest", {})
    kernel_path = resolve_project_path(root, str(kernel_manifest_path))
    if (kernel_record.get("path") != project_relative(root, kernel_path)
            or not kernel_path.is_file()
            or kernel_record.get("sha256") != sha256(kernel_path)):
        raise ValueError("BootInfo probe evidence is stale for the kernel manifest")

    flags_record = manifest.get("target_flags", {})
    flags_path = resolve_project_path(root, flags_record.get("path", ""))
    if not flags_path.is_file() or sha256(flags_path) != flags_record.get("sha256"):
        raise ValueError("BootInfo probe target flags are missing or stale")
    flags_text = flags_path.read_text()
    flags_match = re.search(r"^CXX_FLAGS = (.*)$", flags_text, re.MULTILINE)
    includes_match = re.search(r"^CXX_INCLUDES = (.*)$", flags_text, re.MULTILINE)
    if (not flags_match or not includes_match
            or flags_record.get("cxx_flags") != flags_match.group(1)
            or flags_record.get("include_flags") != includes_match.group(1)
            or "-m32" not in shlex.split(flags_match.group(1))
            or "-march=i386" not in shlex.split(flags_match.group(1))):
        raise ValueError("BootInfo probe target flags do not match the evidence")

    dependencies = manifest.get("dependencies", {})
    if not REQUIRED_LAYOUT_INPUTS.issubset(dependencies):
        raise ValueError("BootInfo probe evidence omits required layout inputs")
    for relative, expected in dependencies.items():
        path = resolve_project_path(root, relative)
        if not path.is_file() or sha256(path) != expected:
            raise ValueError(f"BootInfo layout input is missing or stale: {relative}")
    if manifest.get("probe_source") != {
        "path": PROBE_SOURCE,
        "sha256": dependencies.get(PROBE_SOURCE),
    }:
        raise ValueError("BootInfo probe source record is inconsistent")

    depfile_record = manifest.get("depfile", {})
    depfile_path = resolve_project_path(root, depfile_record.get("path", ""))
    if (not depfile_path.is_file()
            or sha256(depfile_path) != depfile_record.get("sha256")
            or dependency_paths(root, depfile_path) != sorted(dependencies)):
        raise ValueError("BootInfo probe dependency record is missing or stale")
    object_record = manifest.get("object", {})
    object_path = resolve_project_path(root, object_record.get("path", ""))
    if (not object_path.is_file()
            or sha256(object_path) != object_record.get("sha256")
            or object_path.read_bytes()[:7] != b"\x7fELF\x01\x01\x01"):
        raise ValueError("BootInfo target probe object is missing or stale")
    fixture_record = manifest.get("fixture", {})
    fixture_path = resolve_project_path(root, fixture_record.get("path", ""))
    if (not fixture_path.is_file()
            or sha256(fixture_path) != fixture_record.get("sha256")
            or verify_fixture_bytes(fixture_path) != manifest.get("fixture_size")):
        raise ValueError("BootInfo target fixture is missing, stale, or mismatched")
    if reextract_fixture is not None:
        if not objcopy:
            raise ValueError("objcopy is required to re-extract the target fixture")
        reextract_path = resolve_project_path(root, str(reextract_fixture))
        reextract_path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [objcopy, "--dump-section",
             f".bootinfo_layout_fixture={reextract_path}", str(object_path)],
            check=True,
        )
        if (not reextract_path.is_file()
                or reextract_path.read_bytes() != fixture_path.read_bytes()):
            raise ValueError("BootInfo fixture does not match the recorded target object")
        verify_fixture_bytes(reextract_path)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--objcopy", default="i686-elf-objcopy")
    parser.add_argument("--depfile", type=Path)
    parser.add_argument("--flags", type=Path)
    parser.add_argument("--kernel-manifest", type=Path)
    parser.add_argument("--evidence-manifest", type=Path)
    parser.add_argument("--check-evidence", type=Path,
                        help="verify an existing target-probe evidence manifest")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--reextract-fixture", type=Path)
    args = parser.parse_args()

    if args.check_evidence:
        try:
            verify_evidence(
                args.check_evidence, args.kernel_manifest, args.root,
                args.reextract_fixture, args.objcopy,
            )
        except (OSError, ValueError, KeyError, json.JSONDecodeError,
                subprocess.CalledProcessError) as error:
            print(f"BootInfo probe evidence rejected: {error}", file=sys.stderr)
            return 1
        print(f"BootInfo probe evidence verified: {args.check_evidence}")
        return 0

    if not all((args.object, args.fixture, args.depfile, args.flags,
                args.kernel_manifest, args.evidence_manifest)):
        parser.error(
            "--object, --fixture, --depfile, --flags, --kernel-manifest, "
            "and --evidence-manifest are required when creating evidence"
        )
    objcopy = shutil.which(args.objcopy)
    if objcopy is None:
        parser.error(f"target objcopy is unavailable: {args.objcopy}")
    if not args.object.is_file():
        parser.error(f"target probe object is missing: {args.object}")
    args.fixture.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [objcopy, "--dump-section",
         f".bootinfo_layout_fixture={args.fixture}", str(args.object)],
        check=True,
    )
    try:
        evidence = create_evidence(
            args.root, args.object, args.fixture, args.depfile, args.flags,
            args.kernel_manifest,
        )
    except (OSError, ValueError, KeyError) as error:
        print(f"BootInfo target probe rejected: {error}", file=sys.stderr)
        return 1
    args.evidence_manifest.parent.mkdir(parents=True, exist_ok=True)
    args.evidence_manifest.write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"BootInfo i386 target fixture verified: {evidence['fixture_size']} bytes; "
          f"command length {len(COMMAND_LINE) - 1} at offset {COMMAND_LINE_OFFSET}")
    print(f"BootInfo probe evidence: {args.evidence_manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
