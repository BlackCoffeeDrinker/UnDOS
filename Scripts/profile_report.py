#!/usr/bin/env python3
"""Sum the UnDOS kernel profiler blocks in a serial log and report them.

The kernel (built with -DUNDOS_PROFILE=ON, booted with profile_dump_every=<s>)
writes one block per interval and resets its counters after each block, so the
blocks are added up. --last reports only the last valid block.

    @@UNDOS-PROF BEGIN v1 seq=<dec> timer=TSC hz=<dec> count=<dec> dropped=<dec> unmatched=<dec> anchor=<08x>
    P <fn:08x> <calls> <incl> <self>
    @@UNDOS-PROF END count=<dec> crc=<08x>

`crc` is zlib.crc32 over every P line including its CRLF. Inclusive times are
wall-clock (they include time the thread spent preempted).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

BEGIN = re.compile(rb"@@UNDOS-PROF BEGIN v1 (.*?)\r?\n")
END = re.compile(rb"@@UNDOS-PROF END count=(\d+) crc=([0-9a-fA-F]{8})")
RECORD = re.compile(rb"P ([0-9a-fA-F]{8}) (\d+) (\d+) (\d+)\r\n")


class ProfileError(Exception):
    pass


def parse_header(text):
    fields = {}
    for item in text.decode("ascii", "replace").split():
        key, _, value = item.partition("=")
        fields[key] = value
    return fields


def parse_block(data, begin):
    """Returns (header, records) of the block at `begin`, None if it never ends."""
    end = END.search(data, begin.end())
    if not end:
        return None
    body = data[begin.end():end.start()]
    records = []
    crc_input = bytearray()
    for match in RECORD.finditer(body):
        crc_input += match.group(0)
        records.append((int(match.group(1), 16), int(match.group(2)),
                        int(match.group(3)), int(match.group(4))))
    header = parse_header(begin.group(1))
    count = int(end.group(1))
    crc = int(end.group(2), 16)
    if len(records) != count or int(header.get("count", -1)) != count:
        raise ProfileError(f"record count mismatch: got {len(records)}, expected {count}")
    actual = zlib.crc32(bytes(crc_input)) & 0xFFFFFFFF
    if actual != crc:
        raise ProfileError(f"CRC mismatch: computed {actual:08x}, block says {crc:08x}")
    return header, records


def extract_last_block(data):
    """Returns (header fields, records) of the last complete block."""
    for begin in reversed(list(BEGIN.finditer(data))):
        block = parse_block(data, begin)
        if block:
            return block
    raise ProfileError("no complete @@UNDOS-PROF block found")


def sum_blocks(data, warn=lambda message: None):
    """Adds up every valid block. Returns (last header, records, block count)."""
    totals, header, used, prev_seq = {}, None, 0, None
    dropped = unmatched = 0
    for begin in BEGIN.finditer(data):
        try:
            block = parse_block(data, begin)
        except ProfileError as error:
            warn(f"skipped block: {error}")
            continue
        if not block:
            continue
        header, records = block
        seq = int(header.get("seq", -1))
        if prev_seq is not None and seq >= 0:
            if seq == 0:
                warn("seq restarted at 0 (reboot)")
            elif seq != prev_seq + 1:
                warn(f"missing blocks between seq {prev_seq} and {seq}")
        prev_seq = seq
        used += 1
        dropped += int(header.get("dropped", 0))
        unmatched += int(header.get("unmatched", 0))
        for fn, calls, incl, self in records:
            t = totals.setdefault(fn, [0, 0, 0])
            t[0] += calls
            t[1] += incl
            t[2] += self
    if not used:
        raise ProfileError("no complete @@UNDOS-PROF block found")
    header = dict(header, dropped=str(dropped), unmatched=str(unmatched))
    return header, [(fn, *t) for fn, t in totals.items()], used


def find_addr2line():
    if os.environ.get("ADDR2LINE"):
        return os.environ["ADDR2LINE"]
    return shutil.which("i686-elf-addr2line") or shutil.which("addr2line")


def link_address(elf, symbol):
    nm = os.environ.get("NM") or shutil.which("i686-elf-nm") or shutil.which("nm")
    if not nm:
        return None
    out = subprocess.run([nm, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    return None


def symbolize(elf, addresses):
    tool = find_addr2line()
    if not elf or not tool or not addresses:
        return {a: f"0x{a:08x}" for a in addresses}
    proc = subprocess.run([tool, "-f", "-C", "-e", str(elf)] + [f"0x{a:x}" for a in addresses],
                          capture_output=True, text=True)
    lines = proc.stdout.splitlines()
    names = {}
    for i, addr in enumerate(addresses):
        name = lines[2 * i] if 2 * i < len(lines) else "??"
        names[addr] = name if name != "??" else f"0x{addr:08x}"
    return names


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", default="SERIAL_LOG.txt")
    parser.add_argument("--elf", help="kernel ELF used for addr2line symbolization")
    parser.add_argument("--sort", choices=("self", "incl", "calls"), default="self")
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--last", action="store_true", help="report only the last valid block")
    args = parser.parse_args(argv)

    try:
        data = Path(args.log).read_bytes()
        if args.last:
            header, records = extract_last_block(data)
            blocks = 1
        else:
            header, records, blocks = sum_blocks(
                data, lambda m: print(f"profile_report: {m}", file=sys.stderr))
    except (OSError, ProfileError) as error:
        print(f"profile_report: {error}", file=sys.stderr)
        return 1

    # Runtime addresses may differ from link addresses; the header carries the
    # runtime address of kernel::prof::prof_init as `anchor`.
    delta = 0
    if args.elf and "anchor" in header:
        linked = link_address(args.elf, "_ZN6kernel4prof9prof_initEv")
        if linked is not None:
            delta = int(header["anchor"], 16) - linked
    hz = int(header.get("hz", "0")) or 1
    key = {"calls": 1, "incl": 2, "self": 3}[args.sort]
    records.sort(key=lambda r: r[key], reverse=True)
    shown = records[:args.top]
    names = symbolize(args.elf, [(r[0] - delta) & 0xFFFFFFFF for r in shown])
    total_self = sum(r[3] for r in records) or 1

    print(f"blocks={blocks} timer={header.get('timer')} hz={hz} functions={len(records)} "
          f"dropped={header.get('dropped')} unmatched={header.get('unmatched')} "
          f"relocation_delta={delta:#x}")
    print("incl is wall-clock (includes time preempted)")
    print(f"{'function':48} {'calls':>10} {'incl_us':>14} {'self_us':>14} {'self%':>7}")
    for fn, calls, incl, self in shown:
        name = names[(fn - delta) & 0xFFFFFFFF][:48]
        print(f"{name:48} {calls:>10} {incl * 1e6 / hz:>14.1f} {self * 1e6 / hz:>14.1f} "
              f"{100.0 * self / total_self:>6.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
