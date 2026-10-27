#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_DIR="${UNDOS_IMAGE_DIR:-${SCRIPT_DIR}/build/image}"
QEMU_CPU="${UNDOS_QEMU_CPU:-486}"
QEMU_BOOT="${UNDOS_QEMU_BOOT:-d}"
QEMU_MEMORY="${UNDOS_QEMU_MEMORY:-32}"

case "${QEMU_CPU}" in
  486|pentium) ;;
  *)
    printf 'Unsupported UNDOS_QEMU_CPU: %s\n' "${QEMU_CPU}" >&2
    exit 2
    ;;
esac

case "${QEMU_BOOT}" in
  c|d) ;;
  *)
    printf 'Unsupported UNDOS_QEMU_BOOT: %s\n' "${QEMU_BOOT}" >&2
    exit 2
    ;;
esac

case "${QEMU_MEMORY}" in
  8|16|32) ;;
  *)
    printf 'Unsupported UNDOS_QEMU_MEMORY: %s\n' "${QEMU_MEMORY}" >&2
    exit 2
    ;;
esac

set -- -display "${UNDOS_DISPLAY:-none}" -boot "${QEMU_BOOT}"
if [ -n "${UNDOS_QMP_SOCKET:-}" ]; then
  set -- "$@" -qmp "unix:${UNDOS_QMP_SOCKET},server=on,wait=off"
fi
if [ -n "${UNDOS_QEMU_DEBUG_LOG:-}" ]; then
  set -- "$@" -d guest_errors,cpu_reset -D "${UNDOS_QEMU_DEBUG_LOG}"
fi
if [ -f "${IMAGE_DIR}/os.iso" ]; then
  set -- "$@" -drive "file=${IMAGE_DIR}/os.iso,media=cdrom,readonly=on"
fi

qemu-system-i386 -machine isapc -cpu "${QEMU_CPU}" -m "${QEMU_MEMORY}" \
  -device isa-serial,chardev=ser0 \
  -drive "file=${IMAGE_DIR}/fat32.img,format=raw,if=ide,index=0" \
  -chardev stdio,id=ser0,signal=off \
  -monitor none \
  -nodefaults \
  -vga "${UNDOS_VGA:-cirrus}" \
  "$@" &

QEMU_PID=$!
(sleep 15 && kill "$QEMU_PID" 2>/dev/null) &
TIMER_PID=$!

trap 'kill "$QEMU_PID" "$TIMER_PID" 2>/dev/null' INT TERM
wait "$QEMU_PID"
RESULT=$?
kill "$TIMER_PID" 2>/dev/null
exit "$RESULT"
