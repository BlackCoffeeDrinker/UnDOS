"""Regression coverage for named CFG1 block pairs and safe hive rewriting."""

import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "configuration_editor"))

from config import author_localmachine_hive
from hive_api import Config, key_hash


class HiveNameTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="undos-hive-")
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "test.hive"

    def test_names_and_values_round_trip_across_block_boundaries(self):
        for count in (0, 1, 56, 57, 112, 127, 128, 170):
            with self.subTest(count=count):
                config = Config(str(self.path))
                config.entries = {}
                for index in range(count):
                    config.set(f"/Key{index:03}", f"payload {index}")
                config.save(force=True)
                expected = config.entries.copy()
                loaded = Config(str(self.path))
                self.assertEqual(loaded.entries, expected)
                loaded.save(force=True)
                self.assertEqual(Config(str(self.path)).entries, expected)

                raw = self.path.read_bytes()
                offset = Config.HEADER_SIZE
                total = 0
                while offset:
                    next_offset = struct.unpack_from("<I", raw, offset)[0]
                    populated = 0
                    for slot in range(Config.INDEX_ENTRIES_PER_BLOCK):
                        entry_offset = offset + 8 + slot * Config.INDEX_ENTRY_SIZE
                        kh, _, name_slot = struct.unpack_from("<QQH", raw, entry_offset)
                        if not kh:
                            continue
                        populated += 1
                        self.assertLess(name_slot, Config.NAME_ENTRIES_PER_BLOCK)
                        name_offset = offset + Config.PAGE_SIZE + 8 + name_slot * Config.NAME_ENTRY_SIZE
                        self.assertEqual(struct.unpack_from("<Q", raw, name_offset)[0], kh)
                        payload_offset, length = struct.unpack_from("<II", raw, entry_offset + 20)
                        self.assertGreaterEqual(payload_offset, offset + 2 * Config.PAGE_SIZE)
                        self.assertLessEqual(payload_offset + length, next_offset or len(raw))
                    self.assertLessEqual(populated, Config.NAME_ENTRIES_PER_BLOCK)
                    total += populated
                    offset = next_offset
                self.assertEqual(total, count)

    def test_default_hive_retains_all_names_after_save(self):
        with contextlib.redirect_stdout(io.StringIO()):
            author_localmachine_hive(str(self.path))
        config = Config(str(self.path))
        expected = {
            "/System/Console/Desktop/Width": "640",
            "/System/Console/Desktop/RefreshRate": "60",
            "/System/Monitors/Default/MaxHorizontalResolution": "1024",
            "/System/Monitors/Default/MaxVerticalResolution": "768",
            "/System/Monitors/Default/MaxRefreshRate": "60",
            "/System/Monitors/Default/Modes/0": "640x480@60",
            "/System/Monitors/Default/Modes/1": "800x600@60",
            "/System/Monitors/Default/Modes/2": "1024x768@60",
        }
        for path, value in expected.items():
            self.assertEqual(config.get(path), value)
        self.assertFalse(any("<" in path for path in config.entries))
        config.set("/System/Console/Desktop/Width", "1024")
        config.save()
        self.assertEqual(Config(str(self.path)).entries, config.entries)

    def test_reads_kernel_style_pairs_without_name_chain_or_header_counts(self):
        config = Config(str(self.path))
        for index in range(60):
            config.set(f"/Key{index:03}", index)
        config.save()
        raw = bytearray(self.path.read_bytes())
        struct.pack_into("<HH", raw, 6, 0, 0)
        struct.pack_into("<I", raw, Config.HEADER_SIZE + Config.PAGE_SIZE, 0)
        self.path.write_bytes(raw)
        self.assertEqual(Config(str(self.path)).entries, config.entries)

    def test_unnamed_keys_are_displayed_but_cannot_be_rehashed_on_save(self):
        for name_index in (Config.NO_NAME_ENTRY, 55):
            with self.subTest(name_index=name_index):
                self.path.unlink(missing_ok=True)
                config = Config(str(self.path))
                config.set("/Width", "1024")
                config.save()
                raw = bytearray(self.path.read_bytes())
                struct.pack_into("<H", raw, Config.HEADER_SIZE + 8 + 16, name_index)
                self.path.write_bytes(raw)
                loaded = Config(str(self.path))
                self.assertEqual(loaded.get(f"/<{key_hash('Width'):016x}>"), "1024")
                with self.assertRaisesRegex(ValueError, "unresolved key names"):
                    loaded.save(force=True)
                self.assertEqual(self.path.read_bytes(), raw)
                with contextlib.redirect_stdout(io.StringIO()):
                    author_localmachine_hive(str(self.path))
                regenerated = Config(str(self.path))
                self.assertEqual(regenerated.get("/System/Console/Desktop/Width"), "640")
                self.assertFalse(regenerated._unnamed_paths)


if __name__ == "__main__":
    unittest.main()
