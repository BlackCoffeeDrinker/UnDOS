#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

qemu-system-i386 -machine isapc -cpu 486 -m 32 \
  -device isa-serial,chardev=ser0 \
  -hda "${SCRIPT_DIR}/bin/fat32.img" \
  -chardev stdio,id=ser0,signal=off \
  -nographic \
  -nodefaults \
  -drive file="${SCRIPT_DIR}/bin/os.iso",media=cdrom,readonly=on \
  -boot d

