#!/usr/bin/env python3
"""Audit the isolated kernel-entry VGA hold in an emitted i386 kernel."""

import argparse
import re
import shutil
import subprocess
from pathlib import Path


EXPECTED_TEXT = b"UDL DIAG kernel STOP=kernel RESET AFTER OBSERVATION"
INSTRUCTION = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+"
    r"([a-z][a-z0-9.]*)\s*(.*?)\s*$"
)


def objdump_tool():
    return shutil.which("i686-elf-objdump") or shutil.which("objdump")


def disassembly(path, tool):
    if not tool:
        raise ValueError("No compatible objdump is available for the kernel audit")
    return subprocess.run(
        [tool, "-d", "-C", str(path)],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout


def function_instructions(text, symbol):
    marker = f"<{symbol}"
    lines = text.splitlines()
    start = next(
        (index for index, line in enumerate(lines) if marker in line and line.rstrip().endswith(":")),
        None,
    )
    if start is None:
        raise ValueError(f"Kernel entry-stop audit cannot find {symbol}")
    result = []
    for line in lines[start + 1:]:
        if line and not line[0].isspace() and line.rstrip().endswith(":"):
            break
        match = INSTRUCTION.match(line)
        if match:
            result.append(
                (int(match.group(1), 16), match.group(2), match.group(3))
            )
    if not result:
        raise ValueError(f"Kernel entry-stop audit found no instructions for {symbol}")
    return result


def verify(path, object_path):
    tool = objdump_tool()
    text = disassembly(path, tool)
    entry = function_instructions(text, "kernel_core_main")
    transfer_index = next(
        (
            index for index, (_, mnemonic, operands) in enumerate(entry)
            if mnemonic.startswith(("call", "jmp"))
            and "undos_kernel_entry_stop" in operands
        ),
        None,
    )
    if transfer_index is None:
        raise ValueError("kernel_core_main does not transfer first to the stop hook")
    prologue = entry[:transfer_index]
    allowed_prologue = {"push", "mov", "sub", "add", "and", "lea"}
    for _, mnemonic, operands in prologue:
        if mnemonic.startswith("call"):
            if "__x86.get_pc_thunk.ax" not in operands:
                raise ValueError("kernel_core_main calls non-prologue code before the stop hook")
        elif mnemonic not in allowed_prologue:
            raise ValueError(
                f"Unexpected instruction before the kernel stop hook: {mnemonic}"
            )
        if "(" in operands and ")" in operands:
            raise ValueError(
                "kernel_core_main dereferences data before the stop hook"
            )
    if any(mnemonic.startswith("call") for _, mnemonic, _ in prologue):
        thunk = function_instructions(text, "__x86.get_pc_thunk.ax")
        if thunk != [(thunk[0][0], "mov", "(%esp),%eax"),
                     (thunk[1][0], "ret", "")]:
            raise ValueError("Compiler PIC thunk differs from the verified stack prologue")

    hook = function_instructions(text, "undos_kernel_entry_stop")
    stores = []
    terminal = []
    for address, mnemonic, operands in hook:
        if mnemonic.startswith("mov") and "0xb8f" in operands:
            stores.append((mnemonic, operands))
        else:
            terminal.append((address, mnemonic, operands))
    if len(stores) != len(EXPECTED_TEXT):
        raise ValueError(
            f"Kernel stop banner has {len(stores)} VGA stores; "
            f"expected {len(EXPECTED_TEXT)}"
        )
    for index, ((mnemonic, operands), character) in enumerate(
        zip(stores, EXPECTED_TEXT)
    ):
        address = 0xB8F00 + index * 2
        value = 0x4F00 | character
        expected = f"$0x{value:x},0x{address:x}"
        if mnemonic != "movw" or expected not in operands.replace(" ", ""):
            raise ValueError(
                f"Kernel stop banner store {index} differs from its fixed VGA cell"
            )
    meaningful = [item for item in terminal if item[1] != "nop"]
    if not (
        len(meaningful) == 2
        and meaningful[0][1] == "cli"
        and meaningful[1][1].startswith("jmp")
        and re.fullmatch(r"[0-9a-f]+", meaningful[1][2].split()[0])
        and int(meaningful[1][2].split()[0], 16) == meaningful[1][0]
    ):
        raise ValueError(
            f"Kernel stop hook does not end in CLI and a local loop: {terminal!r}"
        )
    relocations = subprocess.run(
        [tool, "-r", "--section=.text.kernel_entry_stop", str(object_path)],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout
    if re.search(r"^[0-9a-f]+\s+R_", relocations, re.MULTILINE):
        raise ValueError("Kernel stop hook contains an unresolved relocation")
    return {
        "kernel": str(path),
        "entry_transfer": entry[transfer_index],
        "entry_prologue": prologue,
        "banner": EXPECTED_TEXT.decode("ascii"),
        "vga_stores": len(stores),
        "hook_terminal": meaningful,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--object", type=Path, required=True)
    args = parser.parse_args()
    result = verify(
        args.kernel.resolve(strict=True), args.object.resolve(strict=True)
    )
    print("Kernel-entry stop audit passed")
    print(f"First-body transfer: {result['entry_transfer']}")
    print(f"Compiler prologue instructions: {len(result['entry_prologue'])}")
    print(f"Direct VGA stores: {result['vga_stores']}")
    print(f"Terminal sequence: {result['hook_terminal']}")


if __name__ == "__main__":
    main()
