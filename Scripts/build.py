#!/usr/bin/env python3
"""Build independent UnDOS projects without using the root IDE launcher."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
APPLICATIONS = {
    "Progman": ("Apps/Progman", ("progman.elf",)),
    "Terminal": ("Apps/Terminal", ("term.elf",)),
    "Demo": ("Apps/Demo", ("demo.elf",)),
    "FileManager": ("Apps/FileManager", ("winfile.elf",)),
    "Init": ("Apps/Init", ("init.elf", "initlib.so")),
    "Fontloader": ("Apps/Fontloader", ("fontload.elf",)),
    "ABI": ("Tests/ABI", ("abitest.elf", "abitest.so", "abinext.so", "abimiss.elf")),
}


def run(*command):
    subprocess.run([str(arg) for arg in command], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=(
        "sdk", "kernel", "apps", "image", "hosted", "docker-toolchain", "docker-sdk"))
    parser.add_argument("--docker", action="store_true", help="Run in the cross-toolchain container")
    parser.add_argument("--toolchain-image", default="gcc-standalone:i686-elf")
    parser.add_argument("--sdk-image", default="undos-sdk:dev")
    parser.add_argument("--build-root", type=Path, default=ROOT / "build")
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--system-files", action="append", type=Path, default=[],
                        help="Explicit directory of extra files to stage under /System")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.docker and args.command not in ("sdk", "kernel", "apps", "image"):
        parser.error("--docker applies only to sdk, kernel, apps, and image")
    build_root = args.build_root.resolve()
    if args.docker:
        relative_build = build_root.relative_to(ROOT)
        command = ["python3", "Scripts/build.py", args.command,
                   "--build-root", str(Path("/src") / relative_build),
                   "--config", args.config, "--jobs", str(args.jobs)]
        for argument in args.cmake_arg:
            command.append("--cmake-arg=" + argument)
        for directory in args.system_files:
            command.extend(("--system-files", str(Path("/src") / directory.resolve().relative_to(ROOT))))
        run("docker", "run", "--rm", "-v", f"{ROOT}:/src", "-w", "/src",
            args.toolchain_image, *command)
        return
    if args.command.startswith("docker-"):
        sdk = args.command == "docker-sdk"
        run("docker", "build", "-f",
            ROOT / ("Docker/SDK/Dockerfile" if sdk else "Docker/Toolchain/Dockerfile"),
            "-t", args.sdk_image if sdk else args.toolchain_image,
            *(["--build-arg", f"TOOLCHAIN_IMAGE={args.toolchain_image}"] if sdk else []),
            ROOT if sdk else ROOT / "Docker/Toolchain")
        return

    sdk_prefix = build_root / "sdk" / args.config

    def cmake(source, name, *options, target=None, cross=True):
        directory = build_root / name / args.config
        configure = ["cmake", "-S", ROOT / source, "-B", directory,
                     f"-DCMAKE_BUILD_TYPE={args.config}", *options]
        if cross:
            toolchain = ("BuildSupport/i686-toolchain.cmake" if source == "Kernel"
                         else "SDK/UnDOS-toolchain.cmake")
            configure.append(f"-DCMAKE_TOOLCHAIN_FILE={ROOT / toolchain}")
        run(*configure, *args.cmake_arg)
        run("cmake", "--build", directory, "--config", args.config,
            "--parallel", args.jobs, *(["--target", target] if target else []))
        return directory

    def sdk():
        cmake("SDK", "sdk-build", f"-DUNDOS_SDK_PREFIX={sdk_prefix}", target="sdk")

    def kernel():
        return cmake("Kernel", "kernel")

    def apps():
        sdk()
        files = {}
        for name, (source, outputs) in APPLICATIONS.items():
            output = build_root / "apps" / name / args.config
            directory = cmake(source, "apps/" + name,
                              f"-DUnDOS_DIR={sdk_prefix / 'share/cmake/UnDOS'}",
                              f"-DUNDOS_SDK_ROOT={sdk_prefix}",
                              f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$<1:{output}>",
                              f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$<1:{output}>")
            for filename in outputs:
                path = directory / filename
                if not path.is_file():
                    raise RuntimeError(f"Missing application artifact: {path}")
                destination = "System/" + filename
                if destination in files:
                    raise RuntimeError(f"Duplicate application artifact: {destination}")
                files[destination] = str(path)
        manifest = build_root / "apps" / f"artifacts-{args.config}.json"
        manifest.write_text(json.dumps({"files": files}, indent=2) + "\n")

    if args.command == "sdk":
        sdk()
    elif args.command == "kernel":
        kernel()
    elif args.command == "apps":
        apps()
    elif args.command == "hosted":
        directory = cmake("Tests/Hosted", "hosted", cross=False)
        run("ctest", "--test-dir", directory, "--build-config", args.config,
            "--output-on-failure", "--parallel", args.jobs)
    elif args.command == "image":
        directory = kernel()
        apps()
        run(sys.executable, ROOT / "Image/assemble.py",
            "--kernel-manifest", directory / f"artifacts-{args.config}.json",
            "--apps-manifest", build_root / "apps" / f"artifacts-{args.config}.json",
            "--output", build_root / "image",
            *(value for directory in args.system_files
              for value in ("--system-files", directory.resolve())))


if __name__ == "__main__":
    main()
