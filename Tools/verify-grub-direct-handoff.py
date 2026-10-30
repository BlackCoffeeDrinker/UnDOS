#!/usr/bin/env python3
"""Audit decoded CPU-preflight and transition control flow in the module."""

import argparse
import re
import subprocess
import sys
from pathlib import Path


SYMBOL = re.compile(r"^([0-9a-fA-F]+) <([^>]+)>:$")
INSTRUCTION = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+(?:(?:[0-9a-fA-F]{2})+\s+)+"
    r"([a-zA-Z][a-zA-Z0-9.]*)\s*(.*?)\s*$"
)


class AuditError(ValueError):
    pass


def decode_functions(disassembly):
    functions = {}
    current = None
    for line in disassembly.splitlines():
        symbol = SYMBOL.match(line)
        if symbol:
            current = symbol.group(2)
            functions.setdefault(current, [])
            continue
        instruction = INSTRUCTION.match(line)
        if instruction and current:
            address, mnemonic, operands = instruction.groups()
            functions[current].append(
                (int(address, 16), mnemonic.lower(), operands.lower())
            )
    return functions


def branch_target(operands):
    match = re.match(r"\s*([0-9a-f]+)\b", operands)
    if not match:
        raise AuditError(f"conditional branch has no decoded target: {operands}")
    return int(match.group(1), 16)


def audit_pre_stack_transition(start, after_far):
    lgdt_indices = [
        i for i, (_, mnemonic, _) in enumerate(start) if mnemonic.startswith("lgdt")
    ]
    if len(lgdt_indices) != 1:
        raise AuditError("ordinary transition must contain exactly one LGDT")
    lgdt_index = lgdt_indices[0]
    stack_setup_indices = [
        i for i, (_, mnemonic, operands) in enumerate(after_far)
        if mnemonic == "mov" and re.match(r"\s*esp,", operands)
    ]
    if not stack_setup_indices:
        raise AuditError("owned physical stack is not installed after the far jump")
    stack_setup_index = stack_setup_indices[0]
    if not any(
        mnemonic == "mov" and re.search(r"\bss\s*,", operands)
        for _, mnemonic, operands in after_far[:stack_setup_index]
    ):
        raise AuditError("owned SS is not installed before the physical stack")

    before_owned_stack = start + after_far[:stack_setup_index]
    stack_instructions = {
        "call", "callf", "lcall", "enter", "leave", "int", "int1", "int3",
        "into", "iret", "iretd", "iretq", "pop", "popa", "popad", "popf",
        "popfd", "popfq", "push", "pusha", "pushad", "pushf", "pushfd",
        "pushfq", "ret", "retf", "sysenter", "sysexit",
    }
    for address, mnemonic, operands in before_owned_stack:
        if mnemonic in stack_instructions or re.match(
            r"(?:call|lcall|ret|iret|push|pop)", mnemonic
        ):
            raise AuditError(
                f"stack-dependent instruction before owned ESP at 0x{address:x}: "
                f"{mnemonic} {operands}".rstrip()
            )
        if re.search(r"\b(?:e?sp)\b", operands):
            raise AuditError(
                f"old stack pointer referenced before owned ESP at 0x{address:x}: "
                f"{mnemonic} {operands}".rstrip()
            )

    allowed_early = {"cli", "cld", "lea", "mov", "jecxz", "jmp", "ljmp"}
    if any(
        mnemonic not in allowed_early and not mnemonic.startswith("lgdt")
        for _, mnemonic, _ in start
    ):
        raise AuditError(
            "pre-LGDT dispatch changes flags or uses an unreviewed instruction"
        )
    if any(mnemonic != "mov" for _, mnemonic, _ in after_far[:stack_setup_index]):
        raise AuditError(
            "segment setup before owned ESP uses an unreviewed instruction"
        )

    early = start[:lgdt_index]
    parameter_fixup = next(
        (
            i for i, (_, mnemonic, operands) in enumerate(early)
            if mnemonic == "lea" and re.fullmatch(r"edi,\[eax-0x([0-9a-f]+)\]", operands)
        ),
        None,
    )
    if parameter_fixup is None:
        raise AuditError("ordinary transition no longer derives parameters from EAX")
    fixup_match = re.fullmatch(
        r"edi,\[eax-0x([0-9a-f]+)\]", early[parameter_fixup][2]
    )
    fixup_delta = fixup_match.group(1)
    expected_dispatch = (
        ("mov", "eax,ecx"),
        ("mov", "ecx"),
        ("lea", "ecx,[ecx-0x7]"),
        ("jecxz", None),
        ("mov", "ecx,eax"),
        ("lea", f"eax,[edi+0x{fixup_delta}]"),
        ("jmp", None),
    )
    dispatch_start = parameter_fixup + 1
    dispatch_end = dispatch_start + len(expected_dispatch)
    if dispatch_end > len(early):
        raise AuditError("stack-free STOP dispatch is truncated before LGDT")
    dispatch = early[dispatch_start:dispatch_end]
    for instruction, (mnemonic, operand_prefix) in zip(dispatch, expected_dispatch):
        _, actual_mnemonic, operands = instruction
        if actual_mnemonic != mnemonic or (
            operand_prefix is not None
            and not operands.startswith(operand_prefix)
        ):
            raise AuditError(
                "stack-free STOP dispatch does not preserve normal registers: "
                f"expected {mnemonic} {operand_prefix or ''}, got "
                f"{actual_mnemonic} {operands}".rstrip()
            )
    stop_read = dispatch[1][2]
    if "[edi+0x24]" not in stop_read:
        raise AuditError("pre-LGDT STOP dispatch does not read the selected STOP")
    stop_branch = dispatch[3]
    marker = next(
        (
            instruction for instruction in early
            if instruction[0] == branch_target(stop_branch[2])
        ),
        None,
    )
    if (
        marker is None
        or marker[1] != "mov"
        or not re.fullmatch(r"esi,0x6", marker[2])
    ):
        raise AuditError("pre-LGDT parameters STOP no longer selects its named hold")
    restore_jump = dispatch[-1]
    if branch_target(restore_jump[2]) != start[lgdt_index][0]:
        raise AuditError("normal STOP-dispatch path does not restore state before LGDT")
    return (
        "pre-stack transition audit passed: no stack use or ESP reference before "
        "owned SS/ESP; EAX, ECX, and EFLAGS preserved on normal dispatch"
    )


def self_test_pre_stack_transition():
    start = [
        (0x00, "cli", ""),
        (0x01, "cld", ""),
        (0x02, "lea", "edi,[eax-0xc0]"),
        (0x08, "mov", "eax,ecx"),
        (0x0a, "mov", "ecx,DWORD PTR [edi+0x24]"),
        (0x0d, "lea", "ecx,[ecx-0x7]"),
        (0x10, "jecxz", "20 <udl_transition_start+0x20>"),
        (0x12, "mov", "ecx,eax"),
        (0x14, "lea", "eax,[edi+0xc0]"),
        (0x1a, "jmp", "2a <udl_transition_start+0x2a>"),
        (0x20, "mov", "esi,0x6"),
        (0x25, "jmp", "100 <udl_transition_diag_hold>"),
        (0x2a, "lgdt", "DWORD PTR [edi]"),
        (0x2d, "ljmp", "FWORD PTR [edi+0x6]"),
    ]
    after_far = [
        (0x30, "mov", "ax,0x18"),
        (0x34, "mov", "ds,eax"),
        (0x36, "mov", "es,eax"),
        (0x38, "mov", "fs,eax"),
        (0x3a, "mov", "gs,eax"),
        (0x3c, "mov", "ss,eax"),
        (0x3e, "mov", "esp,DWORD PTR [edi+0x10]"),
    ]
    audit_pre_stack_transition(start, after_far)

    def rejected(mutated_start, mutated_after_far=after_far):
        try:
            audit_pre_stack_transition(mutated_start, mutated_after_far)
        except AuditError:
            return
        raise AuditError("pre-stack regression self-test accepted an unsafe sequence")

    rejected(start[:3] + [(0x08, "pushf", "")] + start[3:])
    rejected(start[:3] + [(0x08, "pop", "eax")] + start[3:])
    rejected(start[:3] + [(0x08, "mov", "eax,DWORD PTR [esp]")] + start[3:])
    rejected(start[:3] + [(0x08, "add", "eax,0x1")] + start[3:])
    rejected(start, after_far[:2] + [(0x35, "push", "eax")] + after_far[2:])
    rejected(
        start,
        after_far[:2] + [(0x35, "mov", "eax,DWORD PTR [esp]")]
        + after_far[2:],
    )
    altered_restore = list(start)
    altered_restore[8] = (altered_restore[8][0], "lea", "eax,[edi+0xbf]")
    rejected(altered_restore)
    return "pre-stack transition regression self-tests passed (7 unsafe mutations rejected)"


def audit(disassembly, transition_object=None):
    for forbidden in (
        "grub_relocator16_boot",
        "grub_relocator32_boot",
        "grub_relocator64_boot",
    ):
        if forbidden in disassembly:
            raise AuditError(f"CPU-specific GRUB boot tail is reachable/imported: {forbidden}")
    if "grub_relocator_prepare_relocs" not in disassembly:
        raise AuditError("the generic relocator preparation API is absent")

    functions = decode_functions(disassembly)
    for name in (
        "udl_cpu_preflight",
        "udl_transfer_relocations",
        "udl_transition_start",
        "udl_transition_after_far",
        "udl_raw_gate_start",
    ):
        if name not in functions:
            raise AuditError(f"decoded module is missing {name}")

    preflight = functions["udl_cpu_preflight"]
    cpuid_indices = [i for i, (_, mnemonic, _) in enumerate(preflight)
                     if mnemonic == "cpuid"]
    if len(cpuid_indices) != 2:
        raise AuditError("expected exactly two CPUID instructions in CPU preflight")
    all_cpuid = [
        name for name, body in functions.items()
        if any(mnemonic == "cpuid" for _, mnemonic, _ in body)
    ]
    if all_cpuid != ["udl_cpu_preflight"]:
        raise AuditError(f"CPUID appears outside the guarded preflight: {all_cpuid}")

    vm_check = next(
        (i for i, (_, mnemonic, operands) in enumerate(preflight)
         if mnemonic == "test" and "0x20000" in operands),
        None,
    )
    id_check = next(
        (i for i, (_, mnemonic, operands) in enumerate(preflight)
         if mnemonic == "test" and "0x200000" in operands),
        None,
    )
    if vm_check is None or id_check is None or vm_check >= id_check:
        raise AuditError("VM86 rejection or EFLAGS.ID test is missing or out of order")
    if not any(mnemonic == "popf" for _, mnemonic, _ in preflight[vm_check:id_check]):
        raise AuditError("original EFLAGS are not restored before the ID result is used")
    first_cpuid = cpuid_indices[0]
    guard = next(
        (item for item in preflight[id_check + 1:first_cpuid]
         if item[1] in ("je", "jz")),
        None,
    )
    if guard is None:
        raise AuditError("the EFLAGS.ID result does not guard the first CPUID")
    if branch_target(guard[2]) <= preflight[cpuid_indices[-1]][0]:
        raise AuditError("the ID-immutable branch does not skip every CPUID instruction")

    leaf_check = next(
        (i for i, (_, mnemonic, operands) in enumerate(preflight)
         if mnemonic == "cmp" and "0x1" in operands),
        None,
    )
    if leaf_check is None or leaf_check >= cpuid_indices[-1]:
        raise AuditError("CPUID leaf 1 is not guarded by the maximum-leaf check")
    leaf_guard = next(
        (item for item in preflight[leaf_check + 1:cpuid_indices[-1]]
         if item[1] in ("jb", "jnae", "jc")),
        None,
    )
    if leaf_guard is None or branch_target(leaf_guard[2]) <= preflight[cpuid_indices[-1]][0]:
        raise AuditError("the maximum-basic-leaf branch does not skip CPUID leaf 1")

    transition = functions["udl_transition_after_far"]
    cr4_indices = [
        i for i, (_, mnemonic, operands) in enumerate(transition)
        if mnemonic == "mov" and "cr4" in operands
    ]
    if len(cr4_indices) != 2:
        raise AuditError("expected one CR4 read/write pair in the owned transition")
    mask_test = next(
        (i for i, (_, mnemonic, operands) in enumerate(transition)
         if mnemonic == "test" and operands.endswith(",eax")),
        None,
    )
    if mask_test is None:
        raise AuditError("the owned cleanup mask is not tested")
    mask_guard = next(
        (item for item in transition[mask_test + 1:cr4_indices[0]]
         if item[1] in ("je", "jz")),
        None,
    )
    if mask_guard is None:
        raise AuditError("zero cleanup mask does not branch around CR4 access")
    if not (
        transition[mask_test][0] < mask_guard[0]
        < transition[cr4_indices[0]][0]
        < transition[cr4_indices[1]][0]
        < branch_target(mask_guard[2])
    ):
        raise AuditError("zero-mask branch does not skip the complete CR4 pair")

    cr4_functions = [
        name for name, body in functions.items()
        if any(mnemonic == "mov" and "cr4" in operands
               for _, mnemonic, operands in body)
    ]
    if cr4_functions != ["udl_transition_after_far"]:
        raise AuditError(f"CR4 access appears outside the owned guarded transition: {cr4_functions}")
    if any(mnemonic in ("rdmsr", "wrmsr")
           for body in functions.values()
           for _, mnemonic, _ in body):
        raise AuditError("MSR access is present in the direct module")

    bridge = functions["udl_transfer_relocations"]
    if [(mnemonic, operands) for _, mnemonic, operands in bridge[:2]] != [
        ("cli", ""), ("jmp", "eax")
    ]:
        raise AuditError("relocation bridge does not enter the prepared movers without a C return")
    start = functions["udl_transition_start"]
    if [mnemonic for _, mnemonic, _ in start[:2]] != ["cli", "cld"]:
        raise AuditError("owned transition does not disable interrupts and clear DF first")
    pre_stack_audit = audit_pre_stack_transition(
        start, functions["udl_transition_after_far"]
    )

    stop_values = {
        int(operands.rsplit(",", 1)[-1], 16)
        for body in (start, transition)
        for _, mnemonic, operands in body
        if mnemonic == "cmp" and operands.rsplit(",", 1)[-1].startswith("0x")
    }
    stop_values.update(
        int(match.group(1), 16)
        for body in (start, transition)
        for _, mnemonic, operands in body
        if mnemonic == "lea"
        for match in [re.fullmatch(r"ecx,\[ecx-0x([0-9a-f]+)\]", operands)]
        if match
    )
    if not set(range(7, 13)).issubset(stop_values):
        raise AuditError("transition diagnostic checkpoints 7 through 12 are incomplete")
    if not any(
        mnemonic == "mov" and "[ebp+0x28]" in operands
        for _, mnemonic, operands in transition
    ):
        raise AuditError("transition diagnostic name-table pointer is not loaded")
    if not any(
        mnemonic == "mov" and "[edi+0x24]" in operands
        for _, mnemonic, operands in transition
    ):
        raise AuditError("transition diagnostic STOP field is not read")

    raw_gate = functions["udl_raw_gate_start"]
    if len(raw_gate) < 3 or raw_gate[-2][1:] != ("cli", ""):
        raise AuditError("raw copied gate does not write before CLI")
    if raw_gate[-1][1] != "jmp" or branch_target(raw_gate[-1][2]) > raw_gate[-1][0]:
        raise AuditError("raw copied gate does not end in a backward hold")
    stores = raw_gate[:-2]
    if len(stores) != 51:
        raise AuditError("raw copied gate has an unexpected VGA banner length")
    for index, (_, mnemonic, operands) in enumerate(stores):
        expected_address = 0xB8F00 + index * 2
        if (
            mnemonic != "mov"
            or "word ptr ds:" not in operands
            or f"0x{expected_address:x}," not in operands
            or ",0x4f" not in operands
        ):
            raise AuditError(
                f"raw copied gate store {index} is not a bounded VGA word write"
            )
    if transition_object:
        relocations = subprocess.run(
            ["objdump", "-r", str(transition_object)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        ).stdout
        code_relocations = re.findall(
            r"RELOCATION RECORDS FOR \[(\.text\.udl_[^]]+)\]", relocations
        )
        if code_relocations:
            raise AuditError(
                "copied transition sections contain object relocations: "
                + ", ".join(code_relocations)
            )
    return (
        "decoded handoff audit passed: CPU tails absent; CPUID is EFLAGS.ID/"
        "leaf guarded; zero cleanup mask skips CR4; generic transfer is no-return; "
        "diagnostic checkpoints, stack-free state-preserving pre-LGDT dispatch, "
        "and relocation-free raw VGA gate verified; " + pre_stack_audit
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disassembly", type=Path)
    parser.add_argument("--transition-object", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            print(self_test_pre_stack_transition())
        if args.disassembly:
            print(audit(args.disassembly.read_text(), args.transition_object))
        elif not args.self_test:
            parser.error("provide --disassembly or --self-test")
    except (OSError, subprocess.CalledProcessError, AuditError) as error:
        print(f"handoff audit failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
