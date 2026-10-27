#!/usr/bin/env sh

# Runs the current built system with QEMU in foreground so that
# A user could interact with it

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_DIR="${UNDOS_IMAGE_DIR:-${SCRIPT_DIR}/build/image}"

qemu-system-i386 -machine isapc -cpu 486 -m 32 \
  -device isa-serial,chardev=ser0 \
  -drive "file=${IMAGE_DIR}/fat32.img,format=raw,if=ide,index=0" \
  -chardev stdio,id=ser0,signal=off \
  -monitor none \
  -nodefaults \
  -vga "${UNDOS_VGA:-cirrus}" \
  -drive file="${IMAGE_DIR}/os.iso",media=cdrom,readonly=on \
  -boot d "$@"

