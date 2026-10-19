#!/usr/bin/env python3
"""Build a tiny original TrueType fixture for Font Loader registration smoke."""

import argparse
from pathlib import Path
import struct


def checksum(data):
    padded = data + bytes((-len(data)) % 4)
    return sum(struct.unpack(f">{len(padded) // 4}I", padded)) & 0xFFFFFFFF


def make_font():
    # Original geometric outlines, not a usable UI font or copied font data.
    def simple(points, instructions=b""):
        result = struct.pack(">hhhhh", 1, 0, 0, 1000, 1000)
        result += struct.pack(">HH", len(points) - 1, len(instructions)) + instructions
        result += bytes([1] * len(points))
        for axis in (0, 1):
            previous = 0
            for point in points:
                result += struct.pack(">h", point[axis] - previous)
                previous = point[axis]
        return result + bytes(len(result) % 2)

    glyphs = [
        simple([(0, 0), (1000, 0), (1000, 1000), (0, 1000)]),
        simple([(0, 0), (1000, 0), (500, 1000)], b"\x00"),
        b"",
        struct.pack(">hhhhhHHhh", -1, 0, 0, 1000, 1000, 0x0203, 1, 0, 0),
    ]
    offsets = [0]
    for glyph in glyphs:
        offsets.append(offsets[-1] + len(glyph))
    head = bytearray(54)
    struct.pack_into(">IIIIHH", head, 0, 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 2048)
    struct.pack_into(">hhhhHHhhh", head, 36, 0, 0, 1000, 1000, 0, 8, 2, 1, 0)
    hhea = bytearray(36)
    struct.pack_into(">IhhhHhhh", hhea, 0, 0x00010000, 1600, -400, 0, 1200, 0, 200, 1000)
    struct.pack_into(">hH", hhea, 32, 0, 4)
    # format 12 allows an explicit mapping to nonconsecutive glyphs.
    groups = [(32, 32, 2), (65, 65, 1), (66, 66, 3)]
    subtable = struct.pack(">HHIII", 12, 0, 16 + len(groups) * 12, 0, len(groups))
    subtable += b"".join(struct.pack(">III", *group) for group in groups)
    tables = {
        b"head": bytes(head),
        b"hhea": bytes(hhea),
        b"maxp": struct.pack(">I14H", 0x00010000, 4, 4, 1, 3, 1, 2, 0, 0, 0, 0, 2, 1, 1, 1),
        b"hmtx": b"".join(struct.pack(">Hh", advance, 0) for advance in (1200, 1200, 600, 1200)),
        b"cmap": struct.pack(">HHHHI", 0, 1, 3, 10, 12) + subtable,
        b"loca": b"".join(struct.pack(">I", offset) for offset in offsets),
        b"glyf": b"".join(glyphs),
        b"cvt ": struct.pack(">h", 1000),
        b"prep": b"\x00",
    }
    tags = sorted(tables)
    power = 1 << (len(tags).bit_length() - 1)
    result = bytearray(struct.pack(">IHHHH", 0x00010000, len(tags), power * 16,
                                   power.bit_length() - 1, len(tags) * 16 - power * 16))
    offset = 12 + len(tags) * 16
    head_offset = 0
    for tag in tags:
        data = tables[tag]
        result += struct.pack(">4sIII", tag, checksum(data), offset, len(data))
        if tag == b"head":
            head_offset = offset
        offset += (len(data) + 3) & ~3
    for tag in tags:
        data = tables[tag]
        result += data + bytes((-len(data)) % 4)
    struct.pack_into(">I", result, head_offset + 8, (0xB1B0AFBA - checksum(result)) & 0xFFFFFFFF)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_font())


if __name__ == "__main__":
    main()
