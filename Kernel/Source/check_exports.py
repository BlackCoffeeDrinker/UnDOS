#!/usr/bin/env python3
"""Fails when the kernel exports a symbol that is not part of its public interface.

A default-visibility global defined in the kernel image must be declared with
UNDOS_KERNEL_PUBLIC_V1API / UNDOS_HAL_PUBLIC_V1API in a header or source under
one of the scanned roots, or be listed in the allowlist (C++ ABI symbols).

usage: check_exports.py <readelf> <kernel.elf> <allowlist> <root>...
"""
import pathlib
import re
import subprocess
import sys

V1API = re.compile(r'UNDOS_(?:KERNEL|HAL)_PUBLIC_V1API\s*\(\s*(?:[^,()]|\([^()]*\))+?,\s*(\w+)')


def main() -> int:
    readelf, image, allowlist, *roots = sys.argv[1:]
    public = set()
    for root in roots:
        for path in pathlib.Path(root).rglob('*'):
            if path.suffix in ('.h', '.hpp', '.cpp') and path.is_file():
                public.update(V1API.findall(path.read_text(errors='ignore')))
    allowed = {line.split('#', 1)[0].strip() for line in open(allowlist)} - {''}

    output = subprocess.run([readelf, '-sW', image], check=True, capture_output=True, text=True).stdout
    leaked = []
    for line in output.splitlines():
        fields = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(fields) < 8 or fields[4] not in ('GLOBAL', 'WEAK') or fields[5] != 'DEFAULT' or fields[6] == 'UND':
            continue
        name = fields[7]
        if name not in public and name not in allowed:
            leaked.append(name)
    if leaked:
        print(f'{image}: exported symbols that are not UNDOS_KERNEL_PUBLIC_V1API '
              f'(declare them with the macro, hide them, or add them to {allowlist}):', file=sys.stderr)
        for name in sorted(set(leaked)):
            print(f'  {name}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
