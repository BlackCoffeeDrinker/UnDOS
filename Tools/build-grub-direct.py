#!/usr/bin/env python3
"""Build the direct GRUB module with the pinned Alpine target environment."""
import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = ROOT / "build/grub-direct-loader"
PINNED_IMAGE = (
    "alpine@sha256:"
    "6708ca119efec5c3fdcad5dc95d8a28a529aad8debc95872b7e12b3941882f55"
)
PINNED_INPUTS = {
    "grub_bios_apk": (
        "grub-bios-2.14-r0.apk",
        "d31d49952a6361e8a7fe9216a336943e6e5244e929eb1e288da52009c647c5dc",
    ),
    "grub_dev_apk": (
        "grub-dev-2.14-r0.apk",
        "efad8d2188dc080e39f95e93a7270c37905bfa4529344c2f747ad79860267fd1",
    ),
}
GRUB_SOURCE_SHA512 = (
    "5008e3b70072798d2b34301219fe174cc6bcfead40a9ab25fe32f30c62a392ff"
    "b7bf2eb629d481f4a6c19c2925324af3f08d7043e62623e746be2d8b3cbd1182"
)
CONFIG_SHA256 = "60a7b81f39d790c28da344e0e30845bbf7ddf1a8d99c9cbf38e2b364fe2bd133"
MODINFO_SHA256 = "b58563eb74687c2a876fef2361e530e1a2e945468272be381ede7dcaa7519cab"
PACKAGES = (
    "gcc=15.2.0-r5",
    "binutils=2.45.1-r1",
    "make=4.4.1-r4",
    "musl-dev=1.2.6-r3",
    "gawk=5.3.2-r2",
)
GENERATOR_PACKAGES = (
    "autoconf",
    "automake",
    "python3",
)
TOOLCHAIN_RECIPE = (
    f"FROM {PINNED_IMAGE}\n"
    "RUN apk add --no-cache " + " ".join(PACKAGES + GENERATOR_PACKAGES) + "\n"
)
TOOLCHAIN_IMAGE = "undos-grub-toolchain:" + hashlib.sha256(
    TOOLCHAIN_RECIPE.encode()
).hexdigest()[:16]


def require_toolchain():
    result = subprocess.run(
        ["docker", "image", "inspect", TOOLCHAIN_IMAGE],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
    )
    if result.returncode:
        raise ValueError(
            "Local GRUB toolchain unavailable. With internet access, run "
            "./build-hdd.sh --prepare-toolchain first.\n" + result.stderr
        )


def digest(path, algorithm="sha256"):
    hasher = hashlib.new(algorithm)
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def path_in_manifest(value):
    path = Path(value)
    if path.is_absolute() and path.parts[:2] == ("/", "src"):
        return ROOT.joinpath(*path.parts[2:])
    return path if path.is_absolute() else ROOT / path


def verify_bootinfo_probe(probe_manifest_path, kernel_manifest_path):
    helper_path = ROOT / "Tools/verify-bootinfo-layout.py"
    spec = importlib.util.spec_from_file_location("verify_bootinfo_layout", helper_path)
    if spec is None or spec.loader is None:
        raise ValueError(f"Cannot load BootInfo probe verifier: {helper_path}")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    evidence = verifier.verify_evidence(
        probe_manifest_path, kernel_manifest_path, ROOT
    )
    return evidence


def verify_pinned_inputs():
    downloads = BUILD_ROOT / "downloads"
    inputs = {}
    for name, (filename, expected) in PINNED_INPUTS.items():
        path = downloads / filename
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"Missing or mismatched pinned input: {path}")
        inputs[name] = {"path": path.relative_to(ROOT).as_posix(), "sha256": expected}

    source_archive = downloads / "grub-2.14.tar.xz"
    if not source_archive.is_file() or digest(source_archive, "sha512") != GRUB_SOURCE_SHA512:
        raise ValueError(f"Missing or mismatched pinned GRUB source: {source_archive}")
    inputs["grub_source"] = {
        "path": source_archive.relative_to(ROOT).as_posix(),
        "sha512": GRUB_SOURCE_SHA512,
    }

    recipe = downloads / "APKBUILD"
    if not recipe.is_file():
        raise ValueError(f"Missing pinned Alpine recipe: {recipe}")
    recipe_text = recipe.read_text()
    sums_match = re.search(r'(?ms)^sha512sums="\s*(.*?)\s*"', recipe_text)
    if not sums_match:
        raise ValueError("Pinned Alpine recipe has no sha512sums block")
    patches = []
    for line in sums_match.group(1).splitlines():
        match = re.fullmatch(r"\s*([0-9a-f]{128})\s+(\S+)\s*", line)
        if not match:
            raise ValueError("Pinned Alpine recipe has an invalid source checksum")
        expected, filename = match.groups()
        if filename.endswith(".patch"):
            path = downloads / filename
            if not path.is_file() or digest(path, "sha512") != expected:
                raise ValueError(f"Missing or mismatched Alpine patch: {path}")
            patches.append({"path": path.relative_to(ROOT).as_posix(),
                            "sha512": expected})
    if len(patches) != 12:
        raise ValueError(f"Expected 12 pinned Alpine patches, found {len(patches)}")
    inputs["apkbuild"] = {
        "path": recipe.relative_to(ROOT).as_posix(),
        "sha256": digest(recipe),
    }
    inputs["patches"] = patches

    recovered = BUILD_ROOT / "recovered/i386-pc/config.h"
    target_config = BUILD_ROOT / "target/include/config.h"
    for path in (recovered, target_config):
        if not path.is_file() or digest(path) != CONFIG_SHA256:
            raise ValueError(f"Missing or mismatched packaged GRUB configuration: {path}")
    inputs["config"] = {
        "path": recovered.relative_to(ROOT).as_posix(),
        "sha256": CONFIG_SHA256,
    }
    inputs["target_config"] = {
        "path": target_config.relative_to(ROOT).as_posix(),
        "sha256": CONFIG_SHA256,
    }

    shipped_modinfo = ROOT / "Image/BootTools/grub/i386-pc/modinfo.sh"
    if digest(shipped_modinfo) != MODINFO_SHA256:
        raise ValueError("Shipped GRUB module options differ from the pinned input")
    inputs["shipped_modinfo"] = {
        "path": shipped_modinfo.relative_to(ROOT).as_posix(),
        "sha256": MODINFO_SHA256,
    }

    bios_apk = downloads / PINNED_INPUTS["grub_bios_apk"][0]
    repository = ROOT / "Image/BootTools/grub/i386-pc"
    compared = 0
    with tarfile.open(bios_apk, "r:gz") as archive:
        members = {
            member.name.removeprefix("usr/lib/grub/i386-pc/"): member
            for member in archive.getmembers()
            if member.isfile() and member.name.startswith("usr/lib/grub/i386-pc/")
        }
        shipped = {path.name for path in repository.iterdir() if path.is_file()}
        if set(members) != shipped:
            raise ValueError("Shipped i386-pc GRUB file inventory differs from pinned BIOS APK")
        for name, member in members.items():
            source = archive.extractfile(member)
            target = repository / name
            if source is None or not target.is_file() or source.read() != target.read_bytes():
                raise ValueError(f"Shipped GRUB file differs from pinned BIOS APK: {name}")
            compared += 1
    inputs["shipped_runtime_comparison"] = {
        "files_compared": compared,
        "equal": compared,
    }
    return inputs


def section_data(path):
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Direct-loader artifact is not ELF32 little-endian")
    if int.from_bytes(data[16:18], "little") != 1 or int.from_bytes(data[18:20], "little") != 3:
        raise ValueError("Direct-loader artifact is not an i386 relocatable module")
    section_offset = int.from_bytes(data[32:36], "little")
    section_size = int.from_bytes(data[46:48], "little")
    section_count = int.from_bytes(data[48:50], "little")
    names_index = int.from_bytes(data[50:52], "little")
    if section_size != 40 or not section_count or section_count > 4096:
        raise ValueError("Direct-loader ELF section table is invalid")
    if names_index >= section_count or section_offset > len(data):
        raise ValueError("Direct-loader ELF section table is out of range")
    if section_count * section_size > len(data) - section_offset:
        raise ValueError("Direct-loader ELF section table is truncated")
    names_header = data[section_offset + names_index * section_size:
                        section_offset + (names_index + 1) * section_size]
    names_offset = int.from_bytes(names_header[16:20], "little")
    names_size = int.from_bytes(names_header[20:24], "little")
    if names_offset > len(data) or names_size > len(data) - names_offset:
        raise ValueError("Direct-loader ELF section-name table is invalid")
    names = data[names_offset:names_offset + names_size]
    sections = {}
    headers = []
    for index in range(section_count):
        header = data[section_offset + index * section_size:
                      section_offset + (index + 1) * section_size]
        name_offset = int.from_bytes(header[:4], "little")
        if name_offset >= len(names):
            raise ValueError("Direct-loader ELF section name is out of range")
        end = names.find(b"\0", name_offset)
        if end < 0:
            raise ValueError("Direct-loader ELF section name is unterminated")
        name = names[name_offset:end].decode("ascii", "strict")
        section_type = int.from_bytes(header[4:8], "little")
        offset = int.from_bytes(header[16:20], "little")
        size = int.from_bytes(header[20:24], "little")
        if index and section_type != 8 and (
            offset > len(data) or size > len(data) - offset
        ):
            raise ValueError("Direct-loader ELF section exceeds the file")
        if name:
            sections[name] = (
                b"" if section_type == 8 else data[offset:offset + size]
            )
        headers.append(header)
    imports = set()
    for header in headers:
        if int.from_bytes(header[4:8], "little") != 2:
            continue
        offset = int.from_bytes(header[16:20], "little")
        size = int.from_bytes(header[20:24], "little")
        link = int.from_bytes(header[24:28], "little")
        entry_size = int.from_bytes(header[36:40], "little")
        if entry_size != 16 or link >= len(headers):
            raise ValueError("Direct-loader ELF symbol table is invalid")
        string_header = headers[link]
        string_offset = int.from_bytes(string_header[16:20], "little")
        string_size = int.from_bytes(string_header[20:24], "little")
        strings = data[string_offset:string_offset + string_size]
        for position in range(offset, offset + size, entry_size):
            symbol = data[position:position + entry_size]
            if len(symbol) != entry_size or int.from_bytes(symbol[14:16], "little") != 0:
                continue
            name_offset = int.from_bytes(symbol[:4], "little")
            if name_offset >= len(strings):
                raise ValueError("Direct-loader ELF symbol name is out of range")
            end = strings.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("Direct-loader ELF symbol name is unterminated")
            name = strings[name_offset:end].decode("ascii", "strict")
            if name:
                imports.add(name)
    return data, sections, imports


def verify_imports(imports, direct_dependencies):
    core_symbols = {
        line.split()[-1]
        for line in (BUILD_ROOT / "target/grub-core/kernel_syms.lst").read_text().splitlines()
        if line.startswith("defined kernel ")
    }
    providers = {}
    provider_path = BUILD_ROOT / "target/grub-core/shipped-module-providers.txt"
    for line in provider_path.read_text().splitlines():
        match = re.match(r"(.+\.mod):[0-9a-fA-F]+\s+\w\s+(\S+)$", line)
        if match:
            providers[match.group(2)] = Path(match.group(1)).stem
    dependency_map = {}
    for line in (BUILD_ROOT / "target/grub-core/moddep.lst").read_text().splitlines():
        if ":" in line:
            module, dependencies = line.split(":", 1)
            dependency_map[module] = dependencies.split()
    closure = set()

    def include(module):
        for dependency in dependency_map.get(module, []):
            if dependency not in closure:
                closure.add(dependency)
                include(dependency)

    include("undos_direct")
    for dependency in direct_dependencies:
        if dependency not in closure:
            closure.add(dependency)
            include(dependency)
    unresolved = sorted(
        name for name in imports
        if name not in core_symbols and name not in providers
    )
    outside = sorted(
        (name, providers[name]) for name in imports
        if name not in core_symbols and providers.get(name) not in closure
    )
    if unresolved or outside:
        raise ValueError(
            f"GRUB import resolution failed; unresolved={unresolved}, "
            f"providers_outside_dependency_closure={outside}"
        )
    if not set(direct_dependencies).issubset(closure):
        raise ValueError("Direct-loader dependency metadata is not in the target closure")
    return sorted(closure), {
        name: ("kernel" if name in core_symbols else providers[name])
        for name in sorted(imports)
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-manifest", type=Path)
    parser.add_argument("--bootinfo-probe-manifest", type=Path)
    parser.add_argument("--output-manifest", type=Path)
    setup = parser.add_mutually_exclusive_group()
    setup.add_argument("--prepare-toolchain", action="store_true")
    setup.add_argument("--check-toolchain", action="store_true")
    parser.add_argument(
        "--check-probe", action="store_true",
        help="validate probe freshness without building or publishing a module",
    )
    args = parser.parse_args()
    if args.prepare_toolchain:
        subprocess.run(
            ["docker", "build", "--platform", "linux/386",
             "-t", TOOLCHAIN_IMAGE, "-"],
            input=TOOLCHAIN_RECIPE, text=True, check=True,
        )
        return 0
    if args.check_toolchain:
        require_toolchain()
        return 0
    if not all((args.kernel_manifest, args.bootinfo_probe_manifest,
                args.output_manifest)):
        parser.error("kernel, BootInfo probe and output manifests are required")

    kernel_manifest_path = path_in_manifest(str(args.kernel_manifest))
    probe_manifest_path = path_in_manifest(str(args.bootinfo_probe_manifest))
    try:
        probe_evidence = verify_bootinfo_probe(
            probe_manifest_path, kernel_manifest_path
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        parser.error(f"BootInfo target probe evidence rejected: {error}")
    if args.check_probe:
        print(f"BootInfo target probe gate passed: {probe_manifest_path}")
        return 0
    require_toolchain()

    direct_sources = {
        "core/loader_core.c": ROOT / "Boot/GrubDirect/core/loader_core.c",
        "core/loader_core.h": ROOT / "Boot/GrubDirect/core/loader_core.h",
        "core/types.h": ROOT / "Boot/GrubDirect/core/types.h",
        "core/cpu_profile.c": ROOT / "Boot/GrubDirect/core/cpu_profile.c",
        "core/cpu_profile.h": ROOT / "Boot/GrubDirect/core/cpu_profile.h",
        "core/diagnostics.c": ROOT / "Boot/GrubDirect/core/diagnostics.c",
        "core/diagnostics.h": ROOT / "Boot/GrubDirect/core/diagnostics.h",
        "grub_adapter.c": ROOT / "Boot/GrubDirect/grub_adapter.c",
        "bootinfo_wire.h": ROOT / "Boot/GrubDirect/bootinfo_wire.h",
        "transition_layout.h": ROOT / "Boot/GrubDirect/transition_layout.h",
        "transition.S": ROOT / "Boot/GrubDirect/transition.S",
        "cpu_preflight.S": ROOT / "Boot/GrubDirect/cpu_preflight.S",
    }
    source_root = BUILD_ROOT / "source/grub-core/undos_direct"
    target_root = BUILD_ROOT / "target/grub-core"
    for relative, source in direct_sources.items():
        target = source_root / relative
        if not target.parent.is_dir():
            raise ValueError(f"Matching GRUB source preparation is missing: {target.parent}")
        shutil.copy2(source, target)
    for path in (
        BUILD_ROOT / "downloads/grub-2.14.tar.xz",
        BUILD_ROOT / "downloads/APKBUILD",
        BUILD_ROOT / "recovered/i386-pc/config.h",
        BUILD_ROOT / "target/include/config.h",
        target_root / "Makefile",
        target_root / "modinfo.sh",
        target_root / "moddep.lst",
        target_root / "kernel_syms.lst",
        target_root / "shipped-module-providers.txt",
        target_root / "build-grub-module-verifier",
        BUILD_ROOT / "source/COPYING",
    ):
        if not path.is_file():
            raise ValueError(f"Matching GRUB target preparation is missing: {path}")
    module_definition = BUILD_ROOT / "source/grub-core/Makefile.core.def"
    definition_text = module_definition.read_text()
    module_start = definition_text.find("name = undos_direct;")
    module_end = definition_text.find("\n};", module_start)
    if module_start < 0 or module_end < 0:
        raise ValueError("Prepared GRUB source has no undos_direct module definition")
    module_definition_text = definition_text[module_start:module_end]
    required_base_sources = (
        "undos_direct/grub_adapter.c",
        "undos_direct/core/loader_core.c",
        "undos_direct/transition.S",
    )
    if not all(source in module_definition_text for source in required_base_sources):
        raise ValueError("Prepared GRUB source has no undos_direct module definition")
    required_module_sources = required_base_sources + (
        "undos_direct/core/cpu_profile.c",
        "undos_direct/core/diagnostics.c",
        "undos_direct/cpu_preflight.S",
    )
    for source in required_module_sources:
        if source not in module_definition_text:
            definition_text = definition_text[:module_end] + (
                f"\n  common = {source};"
            ) + definition_text[module_end:]
            module_end = definition_text.find("\n};", module_start)
    module_definition.write_text(definition_text)
    inputs = verify_pinned_inputs()
    inputs["module_source_files"] = {
        path.relative_to(ROOT).as_posix(): digest(path)
        for path in direct_sources.values()
    }
    inputs["prepared_source_definition"] = {
        "path": module_definition.relative_to(ROOT).as_posix(),
        "sha256": digest(module_definition),
    }
    private_relocator_header = BUILD_ROOT / "source/include/grub/relocator_private.h"
    inputs["pinned_private_relocator_api"] = {
        "path": private_relocator_header.relative_to(ROOT).as_posix(),
        "sha256": digest(private_relocator_header),
        "symbol": "grub_relocator_prepare_relocs",
    }
    license_notice = BUILD_ROOT / "source/COPYING"
    inputs["license_notice"] = {
        "path": license_notice.relative_to(ROOT).as_posix(),
        "sha256": digest(license_notice),
    }
    target_support_paths = (
        target_root / "Makefile",
        target_root / "modinfo.sh",
        target_root / "moddep.lst",
        target_root / "kernel_syms.lst",
        target_root / "shipped-module-providers.txt",
    )

    kernel_manifest = json.loads(kernel_manifest_path.read_text())
    if kernel_manifest.get("kernel_entry_stop") is True:
        raise ValueError(
            "The normal direct-loader module cannot use the diagnostic kernel manifest"
        )
    files = kernel_manifest.get("files", {})
    kernel_path = path_in_manifest(files["boot/kernel.elf"])
    hal_path = path_in_manifest(files["boot/hal_x86_old.elf"])
    if not kernel_path.is_file() or not hal_path.is_file():
        raise ValueError("Kernel/HAL manifest files are missing")
    if any("undos-boot" in key or "boot_x86_old" in key for key in files):
        raise ValueError("Kernel manifest still contains the archived boot target")
    inputs["kernel_sha256"] = digest(kernel_path)
    inputs["hal_sha256"] = digest(hal_path)
    inputs["bootinfo_layout_probe"] = probe_evidence
    inputs["bootinfo_layout_probe_manifest"] = {
        "path": probe_manifest_path.relative_to(ROOT).as_posix(),
        "sha256": digest(probe_manifest_path),
    }

    BUILD_ROOT.joinpath("tmp").mkdir(parents=True, exist_ok=True)
    log_path = BUILD_ROOT / "logs/grub-direct-build.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    container_script = (
        "set -e; "
        "apk verify /src/build/grub-direct-loader/downloads/"
        "grub-bios-2.14-r0.apk /src/build/grub-direct-loader/downloads/"
        "grub-dev-2.14-r0.apk; "
        "python3 /src/Tools/verify-bootinfo-layout.py "
        f"--check-evidence /src/{probe_manifest_path.relative_to(ROOT).as_posix()} "
        f"--kernel-manifest /src/{kernel_manifest_path.relative_to(ROOT).as_posix()} "
        "--root /src "
        f"--reextract-fixture /src/build/grub-direct-loader/bootinfo_layout_fixture.recheck.bin "
        "--objcopy objcopy; "
        "apk info -v | sort; gcc --version | head -1; ld --version | head -1; "
        "cd /src/build/grub-direct-loader/source; "
        "python3 gentpl.py grub-core/Makefile.core.def "
        "grub-core/Makefile.gcry.def > grub-core/Makefile.core.am.new; "
        "mv grub-core/Makefile.core.am.new grub-core/Makefile.core.am; "
        "automake --gnu grub-core/Makefile; "
        "cd /src/build/grub-direct-loader/target; "
        "./config.status grub-core/Makefile; "
        "cd /src/build/grub-direct-loader/target/grub-core; "
        "export TMPDIR=/src/build/grub-direct-loader/tmp; "
        "make -o syminfo.lst -o moddep.lst "
        "build-grub-module-verifier undos_direct.mod; "
        "./build-grub-module-verifier undos_direct.mod i386 pc; "
        "objdump -dr -M intel undos_direct.mod "
        "> /src/build/grub-direct-loader/logs/undos_direct.disassembly.txt"
    )
    command = [
        "docker", "run", "--rm", "--pull=never", "--network=none",
        "--platform", "linux/386",
        "-v", f"{ROOT}:/src", "-w", "/src", TOOLCHAIN_IMAGE,
        "sh", "-c", container_script,
    ]
    with log_path.open("w") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                       check=True)
    inputs["prepared_target_support"] = {
        path.relative_to(ROOT).as_posix(): digest(path)
        for path in target_support_paths
    }

    module_path = target_root / "undos_direct.mod"
    module, sections, imports = section_data(module_path)
    if (".modname" not in sections or b"undos_direct" not in sections[".modname"]
            or ".moddeps" not in sections
            or ".module_license" not in sections
            or b"LICENSE=GPLv3+" not in sections[".module_license"]):
        raise ValueError("Direct-loader module metadata is incomplete or invalid")
    dependencies = [
        value.decode("ascii")
        for value in sections[".moddeps"].split(b"\0")
        if value
    ]
    for required in ("boot", "mmap", "relocator", "gzio"):
        if required not in dependencies:
            raise ValueError(f"Direct-loader dependency is missing: {required}")
    forbidden_imports = {
        "grub_relocator16_boot",
        "grub_relocator32_boot",
        "grub_relocator64_boot",
    }
    if imports & forbidden_imports:
        raise ValueError(
            "Direct-loader imports a CPU-specific GRUB relocator tail: "
            + ", ".join(sorted(imports & forbidden_imports))
        )
    closure, providers = verify_imports(imports, dependencies)
    if providers.get("grub_relocator_prepare_relocs") != "relocator":
        raise ValueError(
            "Generic relocation preparation is not provided by the shipped relocator module"
        )

    disassembly_path = BUILD_ROOT / "logs/undos_direct.disassembly.txt"
    disassembly = disassembly_path.read_text()
    transition_object = target_root / "undos_direct/module-transition.o"
    if not transition_object.is_file():
        raise ValueError("Built direct-loader transition object is missing")
    handoff_audit = ROOT / "Tools/verify-grub-direct-handoff.py"
    subprocess.run(
        [sys.executable, handoff_audit, "--self-test"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            sys.executable, handoff_audit,
            "--disassembly", disassembly_path,
            "--transition-object", transition_object,
        ],
        cwd=ROOT,
        check=True,
    )
    inputs["handoff_audit_script"] = {
        "path": handoff_audit.relative_to(ROOT).as_posix(),
        "sha256": digest(handoff_audit),
    }
    if "udl_cpu_preflight" not in disassembly:
        raise ValueError("Target disassembly is missing the CPU preflight")
    if "udl_transfer_relocations" not in disassembly:
        raise ValueError("Target disassembly is missing the no-return transfer bridge")
    if "grub_relocator_prepare_relocs" not in disassembly:
        raise ValueError("Target disassembly is missing the generic relocator import")

    modinfo_text = (target_root / "modinfo.sh").read_text()
    makefile = (target_root / "Makefile").read_text()
    option_names = (
        "grub_target_cc", "grub_target_cc_version", "grub_target_cflags",
        "grub_target_cppflags", "grub_target_ccasflags", "grub_target_ldflags",
    )
    target_options = {}
    for name in option_names:
        match = re.search(rf"^{name}='([^']*)'$", modinfo_text, re.MULTILINE)
        if match:
            target_options[name] = match.group(1)
    for name in ("TARGET_CFLAGS", "TARGET_CPPFLAGS", "TARGET_CCASFLAGS"):
        match = re.search(rf"^{name} = (.*)$", makefile, re.MULTILINE)
        if match:
            target_options[name] = match.group(1)
    if target_options.get("grub_target_cc_version") != "gcc (Alpine 15.2.0) 15.2.0":
        raise ValueError("Configured GRUB compiler differs from the pinned target")
    if not all(name in target_options for name in (
        "grub_target_cflags", "grub_target_cppflags",
        "grub_target_ccasflags", "grub_target_ldflags",
        "TARGET_CFLAGS", "TARGET_CPPFLAGS", "TARGET_CCASFLAGS",
    )):
        raise ValueError("Configured GRUB target options are incomplete")

    build_log = log_path.read_text()
    package_inventory = [
        line.strip()
        for line in build_log.splitlines()
        if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9+._-]*-\d[^\s]*", line.strip())
    ]
    if not package_inventory:
        raise ValueError("Pinned Alpine build package inventory was not captured")
    manifest = {
        "schema_version": 1,
        "target": "i386-pc",
        "grub_version": "2.14",
        "verified": True,
        "artifact": {
            "path": module_path.relative_to(ROOT).as_posix(),
            "sha256": digest(module_path),
            "size": len(module),
        },
        "inputs": inputs,
        "config_sha256": CONFIG_SHA256,
        "toolchain": {
            "image": PINNED_IMAGE,
            "prepared_image": TOOLCHAIN_IMAGE,
            "architecture": "linux/386",
            "packages": list(PACKAGES + GENERATOR_PACKAGES),
            "package_inventory": package_inventory,
            "compiler": "gcc (Alpine 15.2.0) 15.2.0",
            "binutils": "2.45.1-r1",
            "make": "4.4.1-r4",
            "build_log": log_path.relative_to(ROOT).as_posix(),
        },
        "target_options": target_options,
        "commands": [
            "apk verify <pinned grub-bios and grub-dev APKs>",
            "use preinstalled packages from " + TOOLCHAIN_IMAGE + " with networking disabled",
            "python3 gentpl.py and automake for the module source definition",
            "config.status grub-core/Makefile with recorded target configuration",
            "make -o syminfo.lst -o moddep.lst "
            "build-grub-module-verifier undos_direct.mod",
            "build-grub-module-verifier undos_direct.mod i386 pc",
            "verify-grub-direct-handoff.py --self-test",
            "verify-grub-direct-handoff.py on decoded module code",
        ],
        "dependencies": {
            "direct": dependencies,
            "closure": closure,
            "imports": providers,
        },
        "handoff": {
            "generic_prepare_symbol": "grub_relocator_prepare_relocs",
            "generic_prepare_provider": providers["grub_relocator_prepare_relocs"],
            "cpu_specific_boot_tail_imports": [],
            "disassembly": disassembly_path.relative_to(ROOT).as_posix(),
            "disassembly_sha256": digest(disassembly_path),
            "transition_object": {
                "path": transition_object.relative_to(ROOT).as_posix(),
                "sha256": digest(transition_object),
            },
            "transition_parameter_delta": 192,
            "baseline_cleanup_mask": 0,
        },
        "license": {
            "module_metadata": "GPLv3+",
            "notice": "build/grub-direct-loader/source/COPYING",
        },
    }
    output_path = path_in_manifest(str(args.output_manifest))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Built {module_path.relative_to(ROOT)}")
    print(f"Verified manifest {output_path.relative_to(ROOT)}")
    print(f"SHA-256 {manifest['artifact']['sha256']}")


if __name__ == "__main__":
    main()
