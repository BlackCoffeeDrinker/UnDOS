#!/usr/bin/env python3
"""Assemble the real CRT entry and check its i386 cdecl calls without booting."""

import argparse
import pathlib
import struct
import subprocess


def verify(object_path):
    data = object_path.read_bytes()
    assert data[:5] == b"\x7fELF\x01", "expected an ELF32 object"
    shoff = struct.unpack_from("<I", data, 32)[0]
    shsize, shcount, shstrings = struct.unpack_from("<HHH", data, 46)
    sections = [
        struct.unpack_from("<10I", data, shoff + i * shsize)
        for i in range(shcount)
    ]
    strings = sections[shstrings]
    names = data[strings[4]:strings[4] + strings[5]]
    text = next(s for s in sections if names[s[0]:].split(b"\0", 1)[0] == b".text")
    code = data[text[4]:text[4] + text[5]]
    relocations = {}
    for section in sections:
        if section[1] != 9 or sections[section[7]] != text:  # SHT_REL for .text
            continue
        symbols = sections[section[6]]
        symbol_strings = sections[symbols[6]]
        symbol_names = data[symbol_strings[4]:symbol_strings[4] + symbol_strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            address, info = struct.unpack_from("<II", data, offset)
            name_offset = struct.unpack_from("<I", data, symbols[4] + (info >> 8) * symbols[9])[0]
            relocations[address] = symbol_names[name_offset:].split(b"\0", 1)[0]

    # Execute the entry's actual opcodes, substituting only external C calls.
    # Unknown instructions fail rather than silently accepting a changed ABI.
    for startup_info in (0x21000, 0x22000, 0x23000):
        esp = initial_esp = 0x20000
        memory = {esp: 0, esp + 4: startup_info, esp + 8: 0}
        eax = ebx = 0
        pc = calls = 0
        while calls < 1:
            if code[pc] == 0xfc:  # cld
                pc += 1
            elif code[pc:pc + 4] == b"\x8b\x44\x24\x04":  # mov 4(%esp), %eax
                eax = memory[esp + 4]
                pc += 4
            elif code[pc:pc + 2] in (b"\x83\xec", b"\x83\xc4"):
                amount = code[pc + 2]
                esp += amount if code[pc + 1] == 0xc4 else -amount
                pc += 3
            elif code[pc] in (0x50, 0x53):  # push eax / ebx
                esp -= 4
                memory[esp] = eax if code[pc] == 0x50 else ebx
                pc += 1
            elif code[pc] == 0xe8:
                assert relocations[pc + 1] == b"crt_start"
                assert esp % 16 == 0, "cdecl call site must be 16-byte aligned"
                # CALL pushes a return address: C arguments are at callee ESP+4.
                callee_esp = esp - 4
                assert memory[callee_esp + 4] == startup_info, "startup pointer corrupted"
                assert memory[initial_esp + 4] == startup_info, "entry frame overwritten"
                calls += 1
                pc += 5
            else:
                raise AssertionError(f"unexpected instruction at {pc}: {code[pc:pc+8].hex()}")
    print("CRT trampoline: versioned startup pointer and call alignment passed (3 cases)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    args.build_dir.mkdir(parents=True, exist_ok=True)
    output = args.build_dir / "process-startup-trampoline-test.o"
    try:
        subprocess.run([
            args.compiler, "--target=i686-unknown-none-elf", "-c",
            str(root / "userland/crt/start.S"), "-o", str(output)
        ], check=True)
        verify(output)
    finally:
        output.unlink(missing_ok=True)
