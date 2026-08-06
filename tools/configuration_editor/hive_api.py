#!/usr/bin/env python3
"""Read/write API for CFG1 configuration hive files matching the kernel layout.

On-disk format (must match kernel/src/configuration_manager.cpp):
  HEADER (32 bytes)
  INDEX BLOCK (4096 bytes): next(4) + reserved(4) + entries[127]*32 + pad(24)
  NAME  BLOCK (4096 bytes): next(4) + reserved(4) + entries[56]*72  + pad(56)
  DATA REGION

Index entry (32 bytes):
  keyHash(u64) parentHash(u64) nameEntryIndex(u16) valueType(u8) reserved0(u8)
  offset(u32) length(u32) reserved1(u32)

Name entry (72 bytes):
  keyHash(u64) name[64]

keyHash is FNV-1a64 of the entry's own short name only, seeded with
ConfigurationKeyTag::seed. parentHash is 0 for top-level entries, otherwise
the parent's keyHash.
"""

from __future__ import annotations

import os
import struct
from typing import Any, Dict, List, Optional, Tuple


# FNV-1a 64-bit constants matching kernel::fnv1a64 / ConfigurationKeyTag.
FNV_OFFSET_BASIS = 14695981039346656037
FNV_PRIME = 1099511628211
CONFIG_KEY_SEED = 18366205674763934000
MASK64 = (1 << 64) - 1


def fnv1a64(data: bytes, seed: int = CONFIG_KEY_SEED) -> int:
    h = seed & MASK64
    for b in data:
        h = (h ^ b) & MASK64
        h = (h * FNV_PRIME) & MASK64
    return h


def key_hash(short_name: str) -> int:
    return fnv1a64(short_name.encode("utf-8"), CONFIG_KEY_SEED)


class Config:
    MAGIC = b"CFG1"
    VERSION = 1
    PAGE_SIZE = 4096
    HEADER_SIZE = 32
    INDEX_ENTRIES_PER_BLOCK = 127
    NAME_ENTRIES_PER_BLOCK = 56
    INDEX_ENTRY_SIZE = 32
    NAME_ENTRY_SIZE = 72
    NO_NAME_ENTRY = 0xFFFF

    VALUE_INVALID = 0
    VALUE_BOOLEAN = 1
    VALUE_UINT32 = 2
    VALUE_UINT64 = 3
    VALUE_SINT32 = 4
    VALUE_SINT64 = 5
    VALUE_STRING = 6

    def __init__(self, filename: str):
        self.filename = filename
        # path -> (value_type, value). Folder keys use VALUE_INVALID / None.
        self.entries: Dict[str, Tuple[int, Any]] = {}
        self.modified = False
        if os.path.exists(filename):
            self.load()

    @staticmethod
    def _normalize_path(path: str) -> str:
        path = path.strip()
        if not path.startswith("/"):
            path = "/" + path
        # Collapse duplicate slashes and strip trailing slash (except root).
        parts = [p for p in path.split("/") if p]
        return "/" + "/".join(parts) if parts else "/"

    @staticmethod
    def _split_path(path: str) -> List[str]:
        path = Config._normalize_path(path)
        return [p for p in path.split("/") if p]

    @staticmethod
    def _parent_path(path: str) -> str:
        parts = Config._split_path(path)
        if len(parts) <= 1:
            return "/"
        return "/" + "/".join(parts[:-1])

    @staticmethod
    def _short_name(path: str) -> str:
        parts = Config._split_path(path)
        return parts[-1] if parts else ""

    def _ensure_folder_chain(self, path: str) -> None:
        """Ensure every intermediate path component exists as a folder entry."""
        parts = self._split_path(path)
        for i in range(len(parts)):
            component_path = "/" + "/".join(parts[: i + 1])
            if component_path not in self.entries:
                self.entries[component_path] = (self.VALUE_INVALID, None)
                self.modified = True
            else:
                # Intermediate components must stay folder-like if they were
                # previously only created as placeholders.
                pass

    def _read_header(self, f) -> Tuple[int, int, int, int, int, int]:
        f.seek(0)
        header = f.read(self.HEADER_SIZE)
        if len(header) < self.HEADER_SIZE:
            raise ValueError("Truncated header")

        magic = header[0:4]
        if magic != self.MAGIC:
            raise ValueError(f"Invalid magic: {magic!r}")

        version = struct.unpack("<H", header[4:6])[0]
        index_count = struct.unpack("<H", header[6:8])[0]
        name_count = struct.unpack("<H", header[8:10])[0]
        index_offset = struct.unpack("<I", header[16:20])[0]
        name_offset = struct.unpack("<I", header[20:24])[0]
        data_offset = struct.unpack("<I", header[24:28])[0]
        return version, index_count, name_count, index_offset, name_offset, data_offset

    def _read_index_blocks(self, f, offset: int, count: int) -> list:
        entries = []
        current_offset = offset

        while current_offset != 0 and len(entries) < count:
            f.seek(current_offset)
            next_offset = struct.unpack("<I", f.read(4))[0]
            f.read(4)  # reserved

            for _ in range(min(self.INDEX_ENTRIES_PER_BLOCK, count - len(entries))):
                entry_data = f.read(self.INDEX_ENTRY_SIZE)
                if len(entry_data) < self.INDEX_ENTRY_SIZE:
                    break
                key_h = struct.unpack("<Q", entry_data[0:8])[0]
                parent_h = struct.unpack("<Q", entry_data[8:16])[0]
                name_index = struct.unpack("<H", entry_data[16:18])[0]
                value_type = entry_data[18]
                data_off = struct.unpack("<I", entry_data[20:24])[0]
                data_len = struct.unpack("<I", entry_data[24:28])[0]

                if key_h != 0:
                    entries.append(
                        {
                            "key_hash": key_h,
                            "parent_hash": parent_h,
                            "name_index": name_index,
                            "value_type": value_type,
                            "data_offset": data_off,
                            "data_length": data_len,
                            "index_block_offset": current_offset,
                        }
                    )

            current_offset = next_offset

        return entries

    def _read_name_blocks(self, f, offset: int, count: int) -> Dict[int, Tuple[int, str]]:
        """Return global name-slot index -> (key_hash, name)."""
        names: Dict[int, Tuple[int, str]] = {}
        current_offset = offset
        idx = 0

        while current_offset != 0 and idx < count:
            f.seek(current_offset)
            next_offset = struct.unpack("<I", f.read(4))[0]
            f.read(4)  # reserved[4]

            for _ in range(min(self.NAME_ENTRIES_PER_BLOCK, count - idx)):
                entry_data = f.read(self.NAME_ENTRY_SIZE)
                if len(entry_data) < self.NAME_ENTRY_SIZE:
                    break
                path_hash = struct.unpack("<Q", entry_data[0:8])[0]
                name_bytes = entry_data[8:72]
                name = name_bytes.split(b"\0", 1)[0].decode("utf-8", errors="replace")
                if path_hash != 0 or name:
                    names[idx] = (path_hash, name)
                idx += 1

            current_offset = next_offset

        return names

    def _decode_value(self, value_type: int, data: bytes) -> Any:
        if value_type == self.VALUE_INVALID:
            return None
        if value_type == self.VALUE_BOOLEAN:
            return struct.unpack("<?", data[:1])[0] if data else False
        if value_type == self.VALUE_UINT32:
            return struct.unpack("<I", data[:4])[0]
        if value_type == self.VALUE_UINT64:
            return struct.unpack("<Q", data[:8])[0]
        if value_type == self.VALUE_SINT32:
            return struct.unpack("<i", data[:4])[0]
        if value_type == self.VALUE_SINT64:
            return struct.unpack("<q", data[:8])[0]
        if value_type == self.VALUE_STRING:
            # Kernel stores raw UTF-8 of exact length (no required NUL).
            return data.decode("utf-8", errors="replace")
        return None

    def load(self) -> None:
        with open(self.filename, "rb") as f:
            _version, index_count, name_count, index_offset, name_offset, _data_offset = (
                self._read_header(f)
            )

            if index_count == 0:
                self.entries = {}
                self.modified = False
                return

            index_entries = self._read_index_blocks(f, index_offset, index_count)
            names = self._read_name_blocks(f, name_offset, name_count)

            # Rebuild hierarchy from parentHash + short names.
            # name_index is per paired NAME BLOCK (0..55), not global across
            # the whole file. For the single-block case used by this tool that
            # is identical to a global index.
            by_hash_parent: Dict[Tuple[int, int], str] = {}
            loaded: List[Tuple[str, int, Any]] = []

            # First pass: resolve each entry's short name and provisional path
            # components. We may need multiple passes to attach under parents.
            raw_items = []
            for entry in index_entries:
                short = ""
                ni = entry["name_index"]
                if ni != self.NO_NAME_ENTRY and ni in names:
                    nh, short = names[ni]
                    if nh != entry["key_hash"]:
                        short = ""
                if not short:
                    # Fallback: cannot reconstruct a meaningful path.
                    short = f"<{entry['key_hash']:016x}>"

                data = b""
                if entry["value_type"] != self.VALUE_INVALID and entry["data_length"]:
                    f.seek(entry["data_offset"])
                    data = f.read(entry["data_length"])
                value = self._decode_value(entry["value_type"], data)
                raw_items.append((entry["key_hash"], entry["parent_hash"], short, entry["value_type"], value))

            # Build paths: parentHash 0 => top-level "/name".
            # parentHash matches parent's keyHash (short-name hash only).
            # When multiple parents share a short-name hash this is ambiguous;
            # the kernel has the same limitation for path reconstruction.
            path_by_keyhash: Dict[int, str] = {}
            # Process in order; parents are expected to appear before children
            # when authored by this tool. Fall back to multi-pass.
            remaining = list(raw_items)
            for _ in range(len(remaining) + 1):
                if not remaining:
                    break
                next_remaining = []
                progress = False
                for key_h, parent_h, short, vtype, value in remaining:
                    if parent_h == 0:
                        path = "/" + short
                    elif parent_h in path_by_keyhash:
                        path = path_by_keyhash[parent_h].rstrip("/") + "/" + short
                    else:
                        next_remaining.append((key_h, parent_h, short, vtype, value))
                        continue
                    path_by_keyhash[key_h] = path
                    # If a keyHash collides across parents, last write wins for
                    # path_by_keyhash lookup of children; store entry by full path.
                    self.entries[path] = (vtype, value)
                    progress = True
                remaining = next_remaining
                if not progress:
                    for key_h, parent_h, short, vtype, value in remaining:
                        path = f"/<orphan_{parent_h:x}>/{short}"
                        self.entries[path] = (vtype, value)
                    break

        self.modified = False

    def get(self, path: str) -> Optional[Any]:
        path = self._normalize_path(path)
        entry = self.entries.get(path)
        return entry[1] if entry else None

    def set(self, path: str, value: Any, value_type: Optional[int] = None) -> None:
        path = self._normalize_path(path)
        if path == "/":
            raise ValueError("Cannot set the hive root itself")

        # Ensure parent folders exist as folder entries.
        parent = self._parent_path(path)
        if parent != "/":
            self._ensure_folder_chain(parent)

        if value_type is None:
            if isinstance(value, bool):
                value_type = self.VALUE_BOOLEAN
            elif isinstance(value, int):
                if value < 0:
                    value_type = self.VALUE_SINT64 if value < -2147483648 else self.VALUE_SINT32
                else:
                    value_type = self.VALUE_UINT64 if value > 4294967295 else self.VALUE_UINT32
            elif isinstance(value, str):
                value_type = self.VALUE_STRING
            elif value is None:
                value_type = self.VALUE_INVALID
            else:
                raise ValueError(f"Cannot auto-detect type for {type(value)}")

        self.entries[path] = (value_type, value)
        self.modified = True

    def ensure_key(self, path: str) -> None:
        """Create a folder-like key (Invalid / no value) at path."""
        path = self._normalize_path(path)
        if path == "/":
            return
        self._ensure_folder_chain(path)
        if path not in self.entries:
            self.entries[path] = (self.VALUE_INVALID, None)
            self.modified = True

    def delete(self, path: str) -> None:
        path = self._normalize_path(path)
        if path in self.entries:
            del self.entries[path]
            self.modified = True

    def list_all(self) -> Dict[str, Any]:
        return {path: value for path, (_, value) in self.entries.items()}

    def _encode_value(self, value_type: int, value: Any) -> bytes:
        if value_type == self.VALUE_INVALID or value is None:
            return b""
        if value_type == self.VALUE_BOOLEAN:
            return struct.pack("<?", bool(value))
        if value_type == self.VALUE_UINT32:
            return struct.pack("<I", int(value) & 0xFFFFFFFF)
        if value_type == self.VALUE_UINT64:
            return struct.pack("<Q", int(value) & MASK64)
        if value_type == self.VALUE_SINT32:
            return struct.pack("<i", int(value))
        if value_type == self.VALUE_SINT64:
            return struct.pack("<q", int(value))
        if value_type == self.VALUE_STRING:
            # Match KE_CM_SetKeyValueString: raw UTF-8, no trailing NUL in length.
            return str(value).encode("utf-8")
        return b""

    def _ordered_entries(self) -> List[Tuple[str, int, Any]]:
        """Return entries in parent-before-child order, then path sort."""
        items = [(self._normalize_path(p), vt, v) for p, (vt, v) in self.entries.items()]
        items.sort(key=lambda t: (t[0].count("/"), t[0]))
        return items

    def _write_config(self, f) -> None:
        items = self._ordered_entries()
        n = len(items)

        index_offset = self.HEADER_SIZE
        num_index_blocks = max(1, (n + self.INDEX_ENTRIES_PER_BLOCK - 1) // self.INDEX_ENTRIES_PER_BLOCK)
        # NAME BLOCKs are paired 1:1 with INDEX BLOCKs in the kernel.
        num_name_blocks = num_index_blocks
        # But name slots are limited to 56 per block; if we have more names
        # than that within a single index block's worth of entries we still
        # only have one paired name block (kernel limitation). For our
        # authoring tool keep entries <= 56 so every entry can be named.
        if n > self.NAME_ENTRIES_PER_BLOCK * num_name_blocks:
            # Grow block pairs so each entry can have a name slot.
            need = (n + self.NAME_ENTRIES_PER_BLOCK - 1) // self.NAME_ENTRIES_PER_BLOCK
            num_index_blocks = max(num_index_blocks, need)
            num_name_blocks = num_index_blocks

        # Layout: header, then for each pair: INDEX then NAME, then data.
        # Kernel expects nameBlockOffset = indexBlockOffset + 4096 for the
        # first pair, and subsequent pairs appended the same way.
        # For multi-block, chain nextBlockOffset through index blocks; each
        # name block is paired immediately after its index block.
        data_offset = index_offset + num_index_blocks * 2 * self.PAGE_SIZE

        header = bytearray(self.HEADER_SIZE)
        header[0:4] = self.MAGIC
        struct.pack_into("<H", header, 4, self.VERSION)
        struct.pack_into("<H", header, 6, n)  # indexEntryCount
        struct.pack_into("<H", header, 8, n)  # nameEntryCount
        first_index = index_offset
        first_name = index_offset + self.PAGE_SIZE  # paired immediately after first index
        struct.pack_into("<I", header, 16, first_index)
        struct.pack_into("<I", header, 20, first_name)
        struct.pack_into("<I", header, 24, data_offset)
        f.write(header)

        data_buffer = bytearray()

        # Precompute hashes.
        meta = []
        for path, value_type, value in items:
            short = self._short_name(path)
            kh = key_hash(short)
            parent = self._parent_path(path)
            if parent == "/":
                ph = 0
            else:
                ph = key_hash(self._short_name(parent))
            payload = self._encode_value(value_type, value)
            meta.append((path, short, kh, ph, value_type, payload))

        # Write block pairs. Entries are distributed across index blocks;
        # name entries use the same slot index within the paired name block.
        for block_idx in range(num_index_blocks):
            index_block_file_offset = index_offset + block_idx * 2 * self.PAGE_SIZE
            name_block_file_offset = index_block_file_offset + self.PAGE_SIZE
            next_index = (
                index_offset + (block_idx + 1) * 2 * self.PAGE_SIZE
                if block_idx < num_index_blocks - 1
                else 0
            )
            next_name = (
                name_block_file_offset + 2 * self.PAGE_SIZE
                if block_idx < num_index_blocks - 1
                else 0
            )

            index_block = bytearray(self.PAGE_SIZE)
            name_block = bytearray(self.PAGE_SIZE)
            struct.pack_into("<I", index_block, 0, next_index)
            struct.pack_into("<I", name_block, 0, next_name)

            for i in range(self.INDEX_ENTRIES_PER_BLOCK):
                entry_idx = block_idx * self.INDEX_ENTRIES_PER_BLOCK + i
                if entry_idx >= n:
                    break
                # Name block only has 56 slots; keep name index in range.
                name_slot = i if i < self.NAME_ENTRIES_PER_BLOCK else self.NO_NAME_ENTRY

                path, short, kh, ph, value_type, payload = meta[entry_idx]
                off = 0
                length = 0
                if value_type != self.VALUE_INVALID and payload:
                    off = data_offset + len(data_buffer)
                    length = len(payload)
                    data_buffer.extend(payload)

                ie_off = 8 + i * self.INDEX_ENTRY_SIZE
                struct.pack_into("<Q", index_block, ie_off + 0, kh)
                struct.pack_into("<Q", index_block, ie_off + 8, ph)
                name_index_val = name_slot if name_slot != self.NO_NAME_ENTRY else self.NO_NAME_ENTRY
                struct.pack_into("<H", index_block, ie_off + 16, name_index_val)
                index_block[ie_off + 18] = value_type & 0xFF
                index_block[ie_off + 19] = 0
                struct.pack_into("<I", index_block, ie_off + 20, off)
                struct.pack_into("<I", index_block, ie_off + 24, length)
                struct.pack_into("<I", index_block, ie_off + 28, 0)

                if name_slot != self.NO_NAME_ENTRY:
                    ne_off = 8 + name_slot * self.NAME_ENTRY_SIZE
                    struct.pack_into("<Q", name_block, ne_off + 0, kh)
                    name_bytes = short.encode("utf-8")[:64]
                    name_bytes = name_bytes + b"\0" * (64 - len(name_bytes))
                    name_block[ne_off + 8 : ne_off + 72] = name_bytes

            f.write(index_block)
            f.write(name_block)

        f.write(data_buffer)

    def save(self, force: bool = False) -> None:
        if not self.modified and not force:
            return
        os.makedirs(os.path.dirname(os.path.abspath(self.filename)) or ".", exist_ok=True)
        with open(self.filename, "wb") as f:
            self._write_config(f)
        self.modified = False
