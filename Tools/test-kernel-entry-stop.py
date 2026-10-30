#!/usr/bin/env python3
"""Run isolated kernel-entry manifest and manual-menu contract tests."""

import importlib.util
import json
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
from grub_direct_diagnostics_menu import STOP_NAMES, render_diagnostic_submenu

spec = importlib.util.spec_from_file_location(
    "update_grub_direct_hdd", ROOT / "Tools/update-grub-direct-hdd.py"
)
if spec is None or spec.loader is None:
    raise RuntimeError("Cannot load the HDD diagnostic manifest verifier")
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-manifest", type=Path, required=True)
    args = parser.parse_args()
    output = ROOT / "build/grub-direct-loader/kernel-entry-stop-tests"
    output.mkdir(parents=True, exist_ok=True)
    source_manifest = args.kernel_manifest.resolve(strict=True)
    source = json.loads(source_manifest.read_text())
    manifests = {
        "enabled": output / "manifest-enabled.json",
        "disabled": output / "manifest-disabled.json",
        "missing": output / "manifest-missing-option.json",
    }
    owned = list(manifests.values())
    if any(path.exists() or path.is_symlink() for path in owned):
        raise RuntimeError(
            "Refusing to overwrite an existing kernel-entry test fixture"
        )
    try:
        manifests["enabled"].write_text(json.dumps(source))
        disabled = dict(source)
        disabled["kernel_entry_stop"] = False
        manifests["disabled"].write_text(json.dumps(disabled))
        missing = dict(source)
        missing.pop("kernel_entry_stop", None)
        manifests["missing"].write_text(json.dumps(missing))

        kernel, record = updater.read_diagnostic_kernel(manifests["enabled"])
        require(kernel.is_file(), "Enabled diagnostic manifest was rejected")
        require(record["kernel_entry_stop"] is True, "Build option was not bound")
        for key in ("disabled", "missing"):
            try:
                updater.read_diagnostic_kernel(manifests[key])
            except ValueError:
                pass
            else:
                raise AssertionError(f"{key} manifest was accepted")

        normal_menu = render_diagnostic_submenu("0123abcd", "3937-4D14")
        require(normal_menu.count('menuentry "STOP ') == len(STOP_NAMES),
                "Normal submenu STOP count changed")
        require("/boot/direct-diag/kernel-entry-stop.elf" not in normal_menu,
                "Normal submenu contains the diagnostic kernel")
        require("undos_direct_diag kernel " not in normal_menu,
                "Normal submenu selects kernel STOP mode")

        diagnostic_menu = render_diagnostic_submenu(
            "0123abcd", "3937-4D14", record["artifact_sha256"][:8]
        )
        require(diagnostic_menu.count('menuentry "STOP ') == len(STOP_NAMES) + 1,
                "Kernel STOP entry was not added exactly once")
        require(diagnostic_menu.count(
            "/boot/direct-diag/kernel-entry-stop.elf"
        ) == 1, "Diagnostic kernel path must occur in only one entry")
        require(diagnostic_menu.count("undos_direct_diag kernel ") == 1,
                "Kernel STOP mode must occur in only one entry")
        for stop in STOP_NAMES:
            require(
                f"undos_direct_diag {stop} /boot/direct/kernel.elf "
                in diagnostic_menu,
                f"Loader STOP {stop} was paired with the diagnostic kernel",
            )
        updater.verify_kernel_stop_pairing(normal_menu.encode(), False)
        updater.verify_kernel_stop_pairing(diagnostic_menu.encode(), True)
        for bad_config in (
            normal_menu + "\nundos_direct_diag kernel /boot/direct/kernel.elf",
            diagnostic_menu.replace(
                "/boot/direct/kernel.elf",
                "/boot/direct-diag/kernel-entry-stop.elf",
                1,
            ),
        ):
            try:
                updater.verify_kernel_stop_pairing(bad_config.encode(), True)
            except ValueError:
                pass
            else:
                raise AssertionError("Mismatched kernel STOP pairing was accepted")
        print(
            "Kernel-entry diagnostic manifest/menu tests passed "
            "(missing/false option, normal pairing, kernel pairing, mismatch)"
        )
    finally:
        for path in owned:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
