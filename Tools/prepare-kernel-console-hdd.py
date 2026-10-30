#!/usr/bin/env python3
"""Prepare a private console HDD candidate; never publish or create a backup."""

import argparse
from contextlib import nullcontext
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SERIAL_DRIVER_IMAGE_PATH = "/System/Drivers/serial.elf"


def validate_existing_vga_menu(config):
    header_pattern = re.compile(
        rb'(?m)^[ \t]*menuentry[ \t]+"[^"\r\n]*"[ \t]+'
        rb'--id=undos-console-vga[ \t]*\{[ \t]*$'
    )
    headers = list(header_pattern.finditer(config))
    if len(headers) != 1 or config.count(b"--id=undos-console-vga") != 1:
        raise ValueError("Source must contain exactly one undos-console-vga entry")

    defaults = re.findall(
        rb"(?m)^[ \t]*set[ \t]+default(?:[ \t]*=|[ \t]+)[^\r\n]*$",
        config,
    )
    normalized_defaults = [line.strip() for line in defaults]
    if (not normalized_defaults
            or normalized_defaults[-1] != b'set default="undos-console-vga"'
            or normalized_defaults.count(b'set default="undos-console-vga"') != 1
            or normalized_defaults[:-1] not in (
                [],
                [b"set default=0"],
            )):
        raise ValueError("Source must retain the undos-console-vga default")

    start = headers[0].end()
    closing = re.search(rb"(?m)^[ \t]*}[ \t]*$", config[start:])
    if closing is None:
        raise ValueError("The undos-console-vga entry is not closed")
    body = config[start:start + closing.start()]
    lines = [line.strip() for line in body.splitlines() if line.strip()]
    expected = [
        b"insmod /boot/grub/i386-pc/undos_direct.mod",
        b"undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf"
        b" -- CMDLINE=12 root=UUID=3937-4D14",
        b"boot",
    ]
    if lines != expected:
        raise ValueError(
            "The undos-console-vga entry does not match the expected direct payloads"
        )


def read_console_manifest_inputs(helper, kernel_manifest, direct_manifest,
                                 apps_manifest=None):
    module, kernel, hal, provenance = helper.read_manifest_inputs(
        kernel_manifest, direct_manifest
    )
    kernel_data = json.loads(kernel_manifest.read_text())
    driver_record = kernel_data.get("files", {}).get("System/Drivers/serial.elf")
    if not driver_record:
        raise ValueError("Kernel manifest omits System/Drivers/serial.elf")
    serial_driver = helper.repo_path(driver_record)
    if not serial_driver.is_file():
        raise ValueError("Serial driver artifact is not a regular file")
    direct_data = json.loads(direct_manifest.read_text())
    probe_record = direct_data.get("inputs", {}).get(
        "bootinfo_layout_probe_manifest", {}
    )
    probe_path = helper.repo_path(probe_record.get("path", ""))
    probe_hash = helper.sha256_file(probe_path)
    if probe_hash != probe_record.get("sha256"):
        raise ValueError("BootInfo probe manifest is missing or stale")
    provenance = dict(provenance)
    provenance["build_manifests"] = dict(provenance["build_manifests"])
    provenance["build_manifests"]["bootinfo_probe"] = {
        "path": probe_path.relative_to(ROOT).as_posix(),
        "sha256": probe_hash,
    }
    provenance["serial_driver"] = {
        "path": serial_driver.relative_to(ROOT).as_posix(),
        "sha256": helper.sha256_file(serial_driver),
    }
    image_files = {}

    def add_file(destination, source):
        path = Path(destination)
        if (path.is_absolute() or ".." in path.parts
                or not destination.startswith("System/")
                or len(path.parts) < 2):
            raise ValueError(f"Invalid System image destination: {destination}")
        target = "/" + path.as_posix()
        if target.casefold() in {key.casefold() for key in image_files}:
            raise ValueError(f"Duplicate image destination: {destination}")
        source = helper.repo_path(source)
        if not source.is_file():
            raise ValueError(f"Missing image input: {source}")
        image_files[target] = {
            "path": source.relative_to(ROOT).as_posix(),
            "sha256": helper.sha256_file(source),
        }

    for destination, source in kernel_data["files"].items():
        if destination.startswith("System/"):
            add_file(destination, source)
    if apps_manifest is not None:
        provenance["build_manifests"]["apps"] = {
            "path": apps_manifest.relative_to(ROOT).as_posix(),
            "sha256": helper.sha256_file(apps_manifest),
        }
        for destination, source in json.loads(apps_manifest.read_text())["files"].items():
            add_file(destination, source)
        for source in ("Apps/Progman/PROGMAN.INI",
                       "Apps/Init/DISPLAY.CFG", "Apps/Init/FONTS.INI"):
            add_file("System/" + Path(source).name, source)
    provenance["image_files"] = image_files
    return module, kernel, hal, serial_driver, provenance


def refresh_payloads(helper, provenance):
    payloads = {
        target: {"sha256": provenance[key]}
        for target, key in zip(helper.DIRECT_PATHS, ("module", "kernel", "hal"))
    }
    payloads.update(provenance["image_files"])
    return payloads


def validate_refresh_inventory(helper, candidate, directories, files, provenance):
    payloads = refresh_payloads(helper, provenance)
    expected_directories = set(directories)
    for target in payloads:
        expected_directories.update(
            parent.as_posix() for parent in Path(target).parents
            if parent.as_posix() != "/"
        )
    candidate_dirs, candidate_files = helper.inventory(candidate)
    if (candidate_dirs != expected_directories
            or candidate_files.keys() != files.keys() | payloads.keys()):
        raise ValueError("Candidate filesystem inventory differs from refresh inputs")
    for path, record in files.items():
        if path not in payloads and candidate_files[path] != record:
            raise ValueError(f"Unrelated image file changed: {path}")
    for path, record in payloads.items():
        if candidate_files[path]["sha256"] != record["sha256"]:
            raise ValueError(f"Candidate payload hash mismatch: {path}")


def prepare_existing_menu_candidate(args, helper, image, kernel_manifest,
                                    direct_manifest, module, kernel, hal, serial_driver,
                                    provenance):
    if args.output.is_symlink():
        raise ValueError("Refresh output must not be a symlink")
    expected_provenance = {
        **provenance,
        "build_manifests": dict(provenance["build_manifests"]),
    }
    task_root = (ROOT / "build/bios-chs-fallback").resolve()
    output = args.output.resolve()
    if output.parent != task_root or output.exists():
        raise ValueError(
            "Refresh output must be a new directory directly under "
            "build/bios-chs-fallback"
        )
    if shutil.disk_usage(task_root).free < image.stat().st_size * 2:
        raise ValueError(
            "Insufficient free space for a refresh candidate and disposable boot copy"
        )

    helper.require_exclusive_image_access(image)
    source_hash = helper.sha256_file(image)
    identity = helper.disk_identity(image)
    directories, files = helper.inventory(image)
    config_path = "/boot/grub/grub.cfg"
    original_config = helper.run_bytes(
        "mtype", "-i", helper.mtools_spec(image), f"::{config_path}"
    )
    validate_existing_vga_menu(original_config)

    direct_data = json.loads(direct_manifest.read_text())
    probe_record = direct_data.get("inputs", {}).get(
        "bootinfo_layout_probe_manifest", {}
    )
    probe_path = helper.repo_path(probe_record.get("path", ""))
    probe_hash = helper.sha256_file(probe_path)
    if probe_hash != probe_record.get("sha256"):
        raise ValueError("BootInfo probe manifest changed after validation")
    provenance["build_manifests"]["bootinfo_probe"] = {
        "path": probe_path.relative_to(ROOT).as_posix(),
        "sha256": probe_hash,
    }

    output.mkdir(parents=True)
    candidate = output / "candidate.img"
    shutil.copy2(image, candidate)
    if helper.sha256_file(candidate) != source_hash:
        raise ValueError("Candidate copy differs from the source image")
    if helper.sha256_file(serial_driver) != provenance["serial_driver"]["sha256"]:
        raise ValueError("Serial driver changed before staging")
    payloads = refresh_payloads(helper, provenance)
    refresh_paths = tuple(payloads)
    refresh_sources = {
        **dict(zip(helper.DIRECT_PATHS, (module, kernel, hal))),
        **{target: helper.repo_path(record["path"])
           for target, record in provenance["image_files"].items()},
    }
    known_directories = set(directories)
    for target, source in refresh_sources.items():
        for parent in reversed(Path(target).parents):
            directory = parent.as_posix()
            if directory == "/" or directory in known_directories:
                continue
            subprocess.run(
                ["mmd", "-i", helper.mtools_spec(candidate), f"::{directory}"],
                check=True,
            )
            known_directories.add(directory)
        subprocess.run(
            ["mcopy", "-o", "-i", helper.mtools_spec(candidate),
             str(source), f"::{target}"],
            check=True,
        )

    validate_refresh_inventory(helper, candidate, directories, files, provenance)
    candidate_config = helper.run_bytes(
        "mtype", "-i", helper.mtools_spec(candidate), f"::{config_path}"
    )
    if candidate_config != original_config:
        raise ValueError("Candidate changed the existing GRUB menu")
    if helper.disk_identity(candidate) != identity:
        raise ValueError("Candidate changed disk identity or boot area")
    if helper.sha256_file(image) != source_hash:
        raise ValueError("Original HDD changed during preparation")

    _, _, _, _, current_provenance = read_console_manifest_inputs(
        helper,
        kernel_manifest, direct_manifest, args.apps_manifest
    )
    if current_provenance != expected_provenance:
        raise ValueError("Producer manifests changed during candidate preparation")
    if helper.sha256_file(probe_path) != probe_hash:
        raise ValueError("BootInfo probe manifest changed during candidate preparation")

    report = {
        "update_kind": "existing-serial-menu-refresh",
        "source_image": str(image),
        "source_sha256": source_hash,
        "candidate_image": str(candidate),
        "candidate_sha256": helper.sha256_file(candidate),
        "identity": identity,
        "provenance": provenance,
        "original_files": files,
        "original_directories": sorted(directories),
        "allowed_changed_paths": sorted(refresh_paths),
        "grub_config_sha256": helper.sha256_bytes(original_config),
        "default_menu_id": "undos-console-vga",
        "kernel_entry_stop": False,
    }
    (output / "candidate-report.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    report_path = output / "candidate-report.json"
    if args.publish:
        boot_output = output / "candidate-boot"
        test = subprocess.run(
            [
                sys.executable,
                str(ROOT / "Tools/test-kernel-console-hdd.py"),
                "--candidate-report", str(report_path),
                "--output", str(boot_output),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        print(test.stdout, end="")
        boot_report_path = boot_output / "boot-report.json"
        boot_report = json.loads(boot_report_path.read_text())
        if (boot_report.get("status") != "PASS"
                or boot_report.get("candidate_sha256") != report["candidate_sha256"]
                or boot_report.get("provenance") != provenance):
            raise ValueError("Candidate boot evidence does not bind this image and provenance")
        report["boot_test"] = {
            "report_path": boot_report_path.relative_to(ROOT).as_posix(),
            "report_sha256": helper.sha256_file(boot_report_path),
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        publish_refreshed_candidate(
            helper, image, candidate, kernel_manifest, direct_manifest,
            expected_provenance, provenance, source_hash, identity,
            directories, files, original_config, report,
        )
    print(json.dumps({
        key: report[key] for key in
        ("candidate_image", "candidate_sha256", "default_menu_id")
    }, indent=2))


def publish_refreshed_candidate(helper, image, candidate, kernel_manifest,
                                direct_manifest, expected_provenance,
                                provenance, source_hash, identity,
                                directories, files, original_config, report):
    _, _, _, _, current_provenance = read_console_manifest_inputs(
        helper, kernel_manifest, direct_manifest,
        helper.repo_path(provenance["build_manifests"]["apps"]["path"])
        if "apps" in provenance["build_manifests"] else None,
    )
    if current_provenance != expected_provenance:
        raise ValueError("Producer manifests changed before publication")

    probe_record = provenance["build_manifests"]["bootinfo_probe"]
    probe_path = helper.repo_path(probe_record["path"])
    if helper.sha256_file(probe_path) != probe_record["sha256"]:
        raise ValueError("BootInfo probe manifest changed before publication")

    candidate_stat = candidate.lstat()
    if not candidate.is_file() or candidate.is_symlink() or candidate_stat.st_nlink != 1:
        raise ValueError("Candidate must be a regular, single-link file")
    candidate_hash = helper.sha256_file(candidate)
    if candidate_hash != report["candidate_sha256"]:
        raise ValueError("Candidate hash changed before publication")

    validate_refresh_inventory(helper, candidate, directories, files, provenance)
    refresh_paths = tuple(refresh_payloads(helper, provenance))

    config_path = "/boot/grub/grub.cfg"
    candidate_config = helper.run_bytes(
        "mtype", "-i", helper.mtools_spec(candidate), f"::{config_path}"
    )
    if (candidate_config != original_config
            or helper.sha256_bytes(candidate_config)
            != report["grub_config_sha256"]):
        raise ValueError("Candidate GRUB menu changed before publication")
    if helper.disk_identity(candidate) != identity:
        raise ValueError("Candidate disk identity changed before publication")

    boot_test = report.get("boot_test", {})
    boot_report_path = helper.repo_path(boot_test.get("report_path", ""))
    if (candidate.parent not in boot_report_path.parents
            or helper.sha256_file(boot_report_path)
            != boot_test.get("report_sha256")):
        raise ValueError("Candidate boot report is missing or changed")
    boot_report = json.loads(boot_report_path.read_text())
    expected_milestones = [
        "UnDOS Kernel INIT with page size",
        "Root UUID 3937-4d14 selected volume",
        "Loading init...",
        "init: default font ready",
        "progman: session ready",
    ]
    if (boot_report.get("status") != "PASS"
            or boot_report.get("candidate_sha256") != report["candidate_sha256"]
            or boot_report.get("source_sha256") != source_hash
            or boot_report.get("provenance") != provenance
            or boot_report.get("allowed_changed_paths") != sorted(refresh_paths)
            or boot_report.get("serial_milestones") != expected_milestones
            or boot_report.get("candidate_unchanged") is not True
            or boot_report.get("provenance_unchanged") is not True
            or boot_report.get("post_readiness", {}).get(
                "serial_bytes_after_ready", 0) <= 0
            or boot_report.get("screenshot", {}).get("width", 0) < 640
            or boot_report.get("screenshot", {}).get("height", 0) < 480
            or boot_report.get("screenshot", {}).get("nonuniform") is not True):
        raise ValueError("Candidate boot report does not satisfy the publication gate")
    for record in (
            boot_report["screenshot"],
            boot_report["post_readiness"],
    ):
        screenshot_path = helper.repo_path(record.get("path") or record.get("screenshot_path", ""))
        if (candidate.parent not in screenshot_path.parents
                or helper.sha256_file(screenshot_path)
                != record.get("sha256", record.get("screenshot_sha256"))):
            raise ValueError("Candidate boot screenshot is missing or changed")
    serial_log = helper.repo_path(
        boot_report["post_readiness"].get("serial_log_path", "")
    )
    if (candidate.parent not in serial_log.parents
            or helper.sha256_file(serial_log)
            != boot_report["post_readiness"].get("serial_log_sha256")):
        raise ValueError("Candidate serial log is missing or changed")

    helper.require_exclusive_image_access(image)
    helper.require_exclusive_image_access(candidate)
    if candidate.stat().st_dev != ROOT.stat().st_dev:
        raise ValueError("Candidate and HDD_IMG.IMG are not on the same filesystem")

    with candidate.open("rb") as source:
        os.fsync(source.fileno())
    if helper.sha256_file(candidate) != report["candidate_sha256"]:
        raise ValueError("Candidate hash changed while syncing")
    if (helper.sha256_file(image) != source_hash
            or helper.disk_identity(image) != identity):
        raise ValueError("Original HDD changed before publication")

    replaced = False
    try:
        os.replace(candidate, image)
        replaced = True
        root_fd = os.open(ROOT, os.O_RDONLY)
        try:
            os.fsync(root_fd)
        finally:
            os.close(root_fd)
        published_hash = helper.sha256_file(image)
        if published_hash != report["candidate_sha256"]:
            raise ValueError("Published HDD hash does not match the candidate")
    except Exception as error:
        if replaced:
            raise RuntimeError(
                "HDD_IMG.IMG was replaced, but post-publication durability "
                f"or hash verification failed: {error}"
            ) from error
        raise

    print(json.dumps({
        "published_image": str(image),
        "published_sha256": published_hash,
    }, indent=2))


def load_helper():
    spec = importlib.util.spec_from_file_location(
        "console_hdd_inputs", ROOT / "Tools/update-grub-direct-hdd.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "HDD_IMG.IMG")
    parser.add_argument("--kernel-manifest", type=Path, required=True)
    parser.add_argument("--direct-manifest", type=Path, required=True)
    parser.add_argument("--apps-manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--refresh-existing", action="store_true",
        help="refresh direct payloads and System artifacts without changing the existing menu",
    )
    parser.add_argument(
        "--publish", action="store_true",
        help="boot-test the disposable candidate, then atomically replace HDD_IMG.IMG",
    )
    args = parser.parse_args()
    if args.publish and not args.refresh_existing:
        parser.error("--publish requires --refresh-existing")
    if args.publish and (args.image.is_symlink()
                         or args.image.absolute() != (ROOT / "HDD_IMG.IMG")):
        parser.error("--publish requires the regular HDD_IMG.IMG target")
    h = load_helper()
    if args.output.is_symlink():
        raise ValueError("Output directory must not be a symlink")
    image = args.image.resolve(strict=True)
    output = args.output.resolve()
    task_root = (ROOT / (
        "build/bios-chs-fallback" if args.refresh_existing
        else "build/kernel-console"
    )).resolve()
    if image != (ROOT / "HDD_IMG.IMG").resolve():
        raise ValueError("Source must be this worktree's private HDD_IMG.IMG")
    if args.refresh_existing:
        invalid_output = output.parent != task_root or output.exists()
    else:
        invalid_output = task_root not in output.parents or output.exists()
    if invalid_output:
        raise ValueError("Output must be a new directory in its task build root")
    lock_context = nullcontext()
    if args.publish:
        lock_root = ROOT / "build/grub-direct-loader/hdd-update"
        lock_root.mkdir(parents=True, exist_ok=True)

        class PublicationLock:
            def __enter__(self):
                self.lock_file = (lock_root / "update.lock").open("a+b")
                try:
                    fcntl.flock(
                        self.lock_file.fileno(),
                        fcntl.LOCK_EX | fcntl.LOCK_NB,
                    )
                except BlockingIOError as error:
                    self.lock_file.close()
                    raise ValueError(
                        "Another direct-loader HDD publication is in progress"
                    ) from error
                return self.lock_file

            def __exit__(self, *_):
                self.lock_file.close()

        lock_context = PublicationLock()
    with lock_context:
        kernel_manifest = args.kernel_manifest.resolve(strict=True)
        direct_manifest = args.direct_manifest.resolve(strict=True)
        if args.apps_manifest is not None:
            args.apps_manifest = args.apps_manifest.resolve(strict=True)
        if json.loads(kernel_manifest.read_text()).get("kernel_entry_stop") is True:
            raise ValueError("A diagnostic entry-stop kernel is not a production input")
        # This shared validator checks current producer hashes, target layout probe
        # dependencies, and the verified GRUB module manifest. Do not bypass it.
        if args.refresh_existing:
            module, kernel, hal, serial_driver, provenance = (
                read_console_manifest_inputs(
                    h, kernel_manifest, direct_manifest, args.apps_manifest)
            )
        else:
            module, kernel, hal, provenance = h.read_manifest_inputs(
                kernel_manifest, direct_manifest
            )
        if args.refresh_existing:
            prepare_existing_menu_candidate(
                args, h, image, kernel_manifest, direct_manifest,
                module, kernel, hal, serial_driver, provenance,
            )
            return
        h.require_exclusive_image_access(image)
        source_hash = h.sha256_file(image)
        identity = h.disk_identity(image)
        directories, files = h.inventory(image)
        config_path = "/boot/grub/grub.cfg"
        original_config = h.run_bytes(
            "mtype", "-i", h.mtools_spec(image), f"::{config_path}"
        )
        if b"undos-console-" in original_config:
            raise ValueError("Source already contains a console candidate menu")
        output.mkdir(parents=True)
        candidate = output / "candidate.img"
        shutil.copy2(image, candidate)
        if h.sha256_file(candidate) != source_hash:
            raise ValueError("Candidate copy differs from the source image")
        for source, target in zip((module, kernel, hal), h.DIRECT_PATHS):
            subprocess.run(
                ["mcopy", "-o", "-i", h.mtools_spec(candidate),
                 str(source), f"::{target}"],
                check=True,
            )
        config = original_config + b"\n# Production kernel console test entries.\n"
        config += (
            '\nmenuentry "UnDOS - minimal VGA kernel test" --id=undos-console-vga {\n'
            "    insmod /boot/grub/i386-pc/undos_direct.mod\n"
            "    undos_direct /boot/direct/kernel.elf /boot/direct/hal_x86_old.elf"
            " -- CMDLINE=12 root=UUID=3937-4D14\n"
            "    boot\n}\n"
        ).encode()
        config += b'\nset default="undos-console-vga"\nset timeout=1\n'
        staged = output / "grub.cfg"
        staged.write_bytes(config)
        subprocess.run(
            ["mcopy", "-o", "-i", h.mtools_spec(candidate),
             str(staged), f"::{config_path}"],
            check=True,
        )
        new_dirs, new_files = h.inventory(candidate)
        if new_dirs != directories or new_files.keys() != files.keys():
            raise ValueError("Candidate changed the original filesystem path inventory")
        changed = set(h.DIRECT_PATHS) | {config_path}
        for path, record in files.items():
            if path not in changed and new_files[path] != record:
                raise ValueError(f"Unrelated image file changed: {path}")
        for path, key in zip(h.DIRECT_PATHS, ("module", "kernel", "hal")):
            if new_files[path]["sha256"] != provenance[key]:
                raise ValueError(f"Candidate payload hash mismatch: {path}")
        if h.disk_identity(candidate) != identity:
            raise ValueError("Candidate changed disk identity or boot area")
        if h.sha256_file(image) != source_hash:
            raise ValueError("Original HDD changed during preparation")
        # Revalidate the producer and target probe after copying all payloads.
        h.read_manifest_inputs(kernel_manifest, direct_manifest)
        report = {
            "source_image": str(image), "source_sha256": source_hash,
            "candidate_image": str(candidate),
            "candidate_sha256": h.sha256_file(candidate),
            "identity": identity, "provenance": provenance,
            "original_files": files, "original_directories": sorted(directories),
            "allowed_changed_paths": sorted(changed),
            "config_sha256": h.sha256_bytes(config),
            "default_menu_id": "undos-console-vga",
            "kernel_entry_stop": False,
        }
        (output / "candidate-report.json").write_text(
            json.dumps(report, indent=2) + "\n"
        )
        print(json.dumps({
            key: report[key] for key in
            ("candidate_image", "candidate_sha256", "default_menu_id")
        }, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
