#!/usr/bin/env python3

import os
import sys
import struct
import hashlib
from typing import Dict, Tuple, Any, Optional


class Config:
    MAGIC = b"CFG1"
    VERSION = 1
    PAGE_SIZE = 4096
    HEADER_SIZE = 32
    INDEX_ENTRIES_PER_BLOCK = 170
    NAME_ENTRIES_PER_BLOCK = 56
    INDEX_ENTRY_SIZE = 24
    NAME_ENTRY_SIZE = 72

    # ValueType enum
    VALUE_INVALID = 0
    VALUE_BOOLEAN = 1
    VALUE_UINT32 = 2
    VALUE_UINT64 = 3
    VALUE_SINT32 = 4
    VALUE_SINT64 = 5
    VALUE_STRING = 6

    def __init__(self, filename: str):
        self.filename = filename
        self.entries: Dict[str, Tuple[int, Any]] = {}  # path -> (value_type, value)
        self.modified = False
        if os.path.exists(filename):
            self.load()

    @staticmethod
    def _hash_path(path: str) -> int:
        """Compute 64-bit hash of path string"""
        return int.from_bytes(hashlib.sha256(path.encode()).digest()[:8], 'little')

    def _read_header(self, f) -> Tuple[int, int, int, int, int]:
        """Read and parse header, return (version, index_count, name_count, index_offset, name_offset, data_offset)"""
        f.seek(0)
        header = f.read(self.HEADER_SIZE)

        magic = header[0:4]
        if magic != self.MAGIC:
            raise ValueError(f"Invalid magic: {magic}")

        version = struct.unpack('<H', header[4:6])[0]
        index_count = struct.unpack('<H', header[6:8])[0]
        name_count = struct.unpack('<H', header[8:10])[0]
        # 6 bytes padding at 10:16
        index_offset = struct.unpack('<I', header[16:20])[0]
        name_offset = struct.unpack('<I', header[20:24])[0]
        data_offset = struct.unpack('<I', header[24:28])[0]

        return version, index_count, name_count, index_offset, name_offset, data_offset

    def _read_index_blocks(self, f, offset: int, count: int) -> list:
        """Read index blocks and return list of index entries"""
        entries = []
        current_offset = offset

        while current_offset != 0 and len(entries) < count:
            f.seek(current_offset)
            next_offset = struct.unpack('<I', f.read(4))[0]
            f.read(4)  # reserved

            for _ in range(min(self.INDEX_ENTRIES_PER_BLOCK, count - len(entries))):
                entry_data = f.read(self.INDEX_ENTRY_SIZE)
                path_hash = struct.unpack('<Q', entry_data[0:8])[0]
                name_index = struct.unpack('<H', entry_data[8:10])[0]
                value_type = entry_data[10]
                # 1 byte padding at entry_data[11]
                data_offset = struct.unpack('<I', entry_data[12:16])[0]
                data_length = struct.unpack('<I', entry_data[16:20])[0]

                if path_hash != 0:  # Valid entry
                    entries.append({
                        'path_hash': path_hash,
                        'name_index': name_index,
                        'value_type': value_type,
                        'data_offset': data_offset,
                        'data_length': data_length
                    })

            current_offset = next_offset

        return entries

    def _read_name_blocks(self, f, offset: int, count: int) -> Dict[int, str]:
        """Read name blocks and return dict mapping index to name"""
        names = {}
        current_offset = offset
        idx = 0

        while current_offset != 0 and idx < count:
            f.seek(current_offset)
            next_offset = struct.unpack('<I', f.read(4))[0]

            for _ in range(min(self.NAME_ENTRIES_PER_BLOCK, count - idx)):
                entry_data = f.read(self.NAME_ENTRY_SIZE)
                path_hash = struct.unpack('<Q', entry_data[0:8])[0]
                name_bytes = entry_data[8:72]
                name = name_bytes.split(b'\0', 1)[0].decode('utf-8')

                if path_hash != 0:  # Valid entry
                    names[idx] = name
                idx += 1

            current_offset = next_offset

        return names

    def _read_data(self, f, offset: int, length: int) -> bytes:
        """Read raw data from data region"""
        f.seek(offset)
        return f.read(length)

    def _decode_value(self, value_type: int, data: bytes) -> Any:
        """Decode value based on type"""
        if value_type == self.VALUE_BOOLEAN:
            return struct.unpack('<?', data[:1])[0]
        elif value_type == self.VALUE_UINT32:
            return struct.unpack('<I', data[:4])[0]
        elif value_type == self.VALUE_UINT64:
            return struct.unpack('<Q', data[:8])[0]
        elif value_type == self.VALUE_SINT32:
            return struct.unpack('<i', data[:4])[0]
        elif value_type == self.VALUE_SINT64:
            return struct.unpack('<q', data[:8])[0]
        elif value_type == self.VALUE_STRING:
            return data.split(b'\0', 1)[0].decode('utf-8')
        else:
            return None

    def load(self):
        """Load configuration from file"""
        with open(self.filename, 'rb') as f:
            version, index_count, name_count, index_offset, name_offset, data_offset = self._read_header(f)

            if index_count == 0:
                return

            index_entries = self._read_index_blocks(f, index_offset, index_count)
            names = self._read_name_blocks(f, name_offset, name_count)

            for entry in index_entries:
                name = names.get(entry['name_index'], '')
                if name:
                    data = self._read_data(f, entry['data_offset'], entry['data_length'])
                    value = self._decode_value(entry['value_type'], data)
                    self.entries[name] = (entry['value_type'], value)

        self.modified = False

    def get(self, path: str) -> Optional[Any]:
        """Get configuration value by path"""
        entry = self.entries.get(path)
        return entry[1] if entry else None

    def set(self, path: str, value: Any, value_type: Optional[int] = None):
        """Set configuration value"""
        if value_type is None:
            # Auto-detect type
            if isinstance(value, bool):
                value_type = self.VALUE_BOOLEAN
            elif isinstance(value, int):
                if value < 0:
                    value_type = self.VALUE_SINT64 if value < -2147483648 else self.VALUE_SINT32
                else:
                    value_type = self.VALUE_UINT64 if value > 4294967295 else self.VALUE_UINT32
            elif isinstance(value, str):
                value_type = self.VALUE_STRING
            else:
                raise ValueError(f"Cannot auto-detect type for {type(value)}")

        self.entries[path] = (value_type, value)
        self.modified = True

    def delete(self, path: str):
        """Delete configuration entry"""
        if path in self.entries:
            del self.entries[path]
            self.modified = True

    def list_all(self) -> Dict[str, Any]:
        """List all configuration entries"""
        return {path: value for path, (_, value) in self.entries.items()}

    def _write_config(self, f):
        """Write configuration to file"""
        # Calculate offsets
        index_offset = self.HEADER_SIZE
        num_index_blocks = (len(self.entries) + self.INDEX_ENTRIES_PER_BLOCK - 1) // self.INDEX_ENTRIES_PER_BLOCK
        num_index_blocks = max(1, num_index_blocks)

        name_offset = index_offset + num_index_blocks * self.PAGE_SIZE
        num_name_blocks = (len(self.entries) + self.NAME_ENTRIES_PER_BLOCK - 1) // self.NAME_ENTRIES_PER_BLOCK
        num_name_blocks = max(1, num_name_blocks)

        data_offset = name_offset + num_name_blocks * self.PAGE_SIZE

        # Write header
        header = bytearray(self.HEADER_SIZE)
        header[0:4] = self.MAGIC
        struct.pack_into('<H', header, 4, self.VERSION)
        struct.pack_into('<H', header, 6, len(self.entries))
        struct.pack_into('<H', header, 8, len(self.entries))
        struct.pack_into('<I', header, 16, index_offset)
        struct.pack_into('<I', header, 20, name_offset)
        struct.pack_into('<I', header, 24, data_offset)
        f.write(header)

        # Prepare entries
        sorted_paths = sorted(self.entries.keys())
        data_buffer = bytearray()

        # Write index blocks
        for block_idx in range(num_index_blocks):
            block = bytearray(self.PAGE_SIZE)
            next_block = index_offset + (block_idx + 1) * self.PAGE_SIZE if block_idx < num_index_blocks - 1 else 0
            struct.pack_into('<I', block, 0, next_block)

            for i in range(self.INDEX_ENTRIES_PER_BLOCK):
                entry_idx = block_idx * self.INDEX_ENTRIES_PER_BLOCK + i
                if entry_idx >= len(sorted_paths):
                    break

                path = sorted_paths[entry_idx]
                value_type, value = self.entries[path]
                path_hash = self._hash_path(path)

                # Encode value
                if value_type == self.VALUE_BOOLEAN:
                    data = struct.pack('<?', value)
                elif value_type == self.VALUE_UINT32:
                    data = struct.pack('<I', value)
                elif value_type == self.VALUE_UINT64:
                    data = struct.pack('<Q', value)
                elif value_type == self.VALUE_SINT32:
                    data = struct.pack('<i', value)
                elif value_type == self.VALUE_SINT64:
                    data = struct.pack('<q', value)
                elif value_type == self.VALUE_STRING:
                    data = value.encode('utf-8') + b'\0'
                else:
                    data = b''

                offset_in_block = 8 + i * self.INDEX_ENTRY_SIZE
                struct.pack_into('<Q', block, offset_in_block, path_hash)
                struct.pack_into('<H', block, offset_in_block + 8, entry_idx)
                block[offset_in_block + 10] = value_type
                struct.pack_into('<I', block, offset_in_block + 12, data_offset + len(data_buffer))
                struct.pack_into('<I', block, offset_in_block + 16, len(data))

                data_buffer.extend(data)

            f.write(block)

        # Write name blocks
        for block_idx in range(num_name_blocks):
            block = bytearray(self.PAGE_SIZE)
            next_block = name_offset + (block_idx + 1) * self.PAGE_SIZE if block_idx < num_name_blocks - 1 else 0
            struct.pack_into('<I', block, 0, next_block)

            for i in range(self.NAME_ENTRIES_PER_BLOCK):
                entry_idx = block_idx * self.NAME_ENTRIES_PER_BLOCK + i
                if entry_idx >= len(sorted_paths):
                    break

                path = sorted_paths[entry_idx]
                path_hash = self._hash_path(path)
                name_bytes = path.encode('utf-8')[:64].ljust(64, b'\0')

                offset_in_block = 4 + i * self.NAME_ENTRY_SIZE
                struct.pack_into('<Q', block, offset_in_block, path_hash)
                block[offset_in_block + 8:offset_in_block + 72] = name_bytes

            f.write(block)

        # Write data region
        f.write(data_buffer)

    def save(self):
        """Save configuration to file"""
        if not self.modified:
            return

        with open(self.filename, 'wb') as f:
            self._write_config(f)

        self.modified = False


def create_database(filename: str):
    """Create an empty configuration database file"""
    with open(filename, 'wb') as f:
        # Write header
        header = bytearray(Config.HEADER_SIZE)
        header[0:4] = Config.MAGIC
        struct.pack_into('<H', header, 4, Config.VERSION)
        struct.pack_into('<H', header, 6, 0)  # index count
        struct.pack_into('<H', header, 8, 0)  # name count
        struct.pack_into('<I', header, 16, Config.HEADER_SIZE)  # index offset
        struct.pack_into('<I', header, 20, Config.HEADER_SIZE + Config.PAGE_SIZE)  # name offset
        struct.pack_into('<I', header, 24, Config.HEADER_SIZE + 2 * Config.PAGE_SIZE)  # data offset
        f.write(header)

        # Write empty index block
        f.write(bytearray(Config.PAGE_SIZE))

        # Write empty name block
        f.write(bytearray(Config.PAGE_SIZE))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python config.py <path-to-config> <command> [args...]")
        sys.exit(1)

    filename = sys.argv[1]
    command = sys.argv[2]

    if command == "create" and not os.path.exists(filename):
        create_database(filename)
        print(f"Created configuration database: {filename}")
        sys.exit(0)

    if not os.path.exists(filename):
        print(f"Error: Configuration file '{filename}' does not exist. Use 'create' command first.")
        sys.exit(1)

    config = Config(filename)

    if command == "create":
        if len(sys.argv) < 5:
            print("Usage: python config.py <path-to-config> create <key> <value> [type]")
            sys.exit(1)
        key = sys.argv[3]
        value_str = sys.argv[4]
        value_type = None

        if len(sys.argv) > 5:
            type_map = {
                'bool': Config.VALUE_BOOLEAN,
                'uint32': Config.VALUE_UINT32,
                'uint64': Config.VALUE_UINT64,
                'sint32': Config.VALUE_SINT32,
                'sint64': Config.VALUE_SINT64,
                'string': Config.VALUE_STRING,
            }
            value_type = type_map.get(sys.argv[5])

        # Parse value
        if value_type == Config.VALUE_BOOLEAN or (value_type is None and value_str.lower() in ['true', 'false']):
            value = value_str.lower() == 'true'
        elif value_type in [Config.VALUE_UINT32, Config.VALUE_UINT64, Config.VALUE_SINT32, Config.VALUE_SINT64] or (
                value_type is None and value_str.lstrip('-').isdigit()):
            value = int(value_str)
        else:
            value = value_str

        config.set(key, value, value_type)
        config.save()
        print(f"Created entry: {key} = {value}")

    elif command == "edit":
        if len(sys.argv) < 5:
            print("Usage: python config.py <path-to-config> edit <key> <value>")
            sys.exit(1)
        key = sys.argv[3]
        value_str = sys.argv[4]

        # Try to preserve type from existing entry
        existing = config.entries.get(key)
        if existing:
            value_type = existing[0]
            if value_type == Config.VALUE_BOOLEAN:
                value = value_str.lower() == 'true'
            elif value_type in [Config.VALUE_UINT32, Config.VALUE_UINT64, Config.VALUE_SINT32, Config.VALUE_SINT64]:
                value = int(value_str)
            else:
                value = value_str
        else:
            # Auto-detect
            if value_str.lower() in ['true', 'false']:
                value = value_str.lower() == 'true'
            elif value_str.lstrip('-').isdigit():
                value = int(value_str)
            else:
                value = value_str
            value_type = None

        config.set(key, value, value_type)
        config.save()
        print(f"Updated entry: {key} = {value}")

    elif command == "delete":
        if len(sys.argv) < 4:
            print("Usage: python config.py <path-to-config> delete <key>")
            sys.exit(1)
        key = sys.argv[3]
        config.delete(key)
        config.save()
        print(f"Deleted entry: {key}")

    elif command == "list":
        entries = config.list_all()
        if not entries:
            print("No entries found.")
        else:
            for key, value in sorted(entries.items()):
                print(f"{key} = {value}")

    elif command == "vacuum":
        # Rewrite the file to compact it
        config.save()
        print(f"Vacuumed configuration database: {filename}")
    else:
        print(f"Unknown command: {command}")
        print("Available commands: create, edit, delete, list, vacuum")
        sys.exit(1)
