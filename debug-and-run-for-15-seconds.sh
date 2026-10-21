#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

python3 "${SCRIPT_DIR}/Scripts/build.py" image --docker --config Debug || exit $?

exec "${SCRIPT_DIR}/run-qemu.sh"
