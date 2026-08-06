#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

docker run --rm -v "${SCRIPT_DIR}:/src" -w /src gcc-standalone:i686-elf cmake --build build --target BootIso -j4

exec "${SCRIPT_DIR}/run-qemu.sh"

