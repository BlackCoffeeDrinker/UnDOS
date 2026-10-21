#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_DIR="${UNDOS_IMAGE_DIR:-${SCRIPT_DIR}/build/image}"

set -- -display "${UNDOS_DISPLAY:-none}"
if [ -n "${UNDOS_QMP_SOCKET:-}" ]; then
  set -- "$@" -qmp "unix:${UNDOS_QMP_SOCKET},server=on,wait=off"
fi

qemu-system-i386 -machine isapc -cpu 486 -m 32 \
  -device isa-serial,chardev=ser0 \
  -drive "file=${IMAGE_DIR}/fat32.img,format=raw,if=ide,index=0" \
  -chardev stdio,id=ser0,signal=off \
  -monitor none \
  -nodefaults \
  -vga "${UNDOS_VGA:-cirrus}" \
  -drive file="${IMAGE_DIR}/os.iso",media=cdrom,readonly=on \
  -boot d "$@" &

QEMU_PID=$!
(sleep 15 && kill "$QEMU_PID" 2>/dev/null) &
TIMER_PID=$!

trap 'kill "$QEMU_PID" "$TIMER_PID" 2>/dev/null' INT TERM
wait "$QEMU_PID"
RESULT=$?
kill "$TIMER_PID" 2>/dev/null
exit "$RESULT"
