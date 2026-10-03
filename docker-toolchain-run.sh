#!/usr/bin/env sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
docker run --rm -v "${SCRIPT_DIR}:/src" -w /src gcc-standalone:i686-elf $*

