#!/usr/bin/env python3
"""Fixture tests for profile_report.py: python3 Scripts/test_profile_report.py"""
import io
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import profile_report  # noqa: E402

RECORDS = b"P c0101000 3 900 600\r\nP c0102000 10 300 300\r\n"


def block(records=RECORDS, crc=None, count=2, seq=0):
    crc = zlib.crc32(records) if crc is None else crc
    return (b"[    1.000000] boot noise\r\n"
            b"@@UNDOS-PROF BEGIN v1 seq=%d timer=PIT hz=1193182 count=%d dropped=0 unmatched=1 anchor=c0100000\r\n"
            % (seq, count) + records + b"@@UNDOS-PROF END count=%d crc=%08x\r\n" % (count, crc))


class ProfileReportTest(unittest.TestCase):
    def run_report(self, data, *extra):
        with tempfile.NamedTemporaryFile(delete=False) as log:
            log.write(data)
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = profile_report.main(["--log", log.name, *extra])
        Path(log.name).unlink()
        return rc, out.getvalue(), err.getvalue()

    def test_good_block(self):
        header, records = profile_report.extract_last_block(block())
        self.assertEqual(header["timer"], "PIT")
        self.assertEqual(records[0], (0xC0101000, 3, 900, 600))
        rc, out, _ = self.run_report(block())
        self.assertEqual(rc, 0)
        self.assertIn("0xc0101000", out)

    def test_bad_crc_fails(self):
        rc, _, err = self.run_report(block(crc=0xDEADBEEF))
        self.assertEqual(rc, 1)
        self.assertIn("CRC mismatch", err)

    def test_last_block_wins(self):
        rc, _, err = self.run_report(block() + block(crc=0xDEADBEEF), "--last")
        self.assertEqual(rc, 1)
        self.assertIn("CRC mismatch", err)
        rc, _, _ = self.run_report(block(crc=0xDEADBEEF) + block(), "--last")
        self.assertEqual(rc, 0)

    def test_blocks_are_summed(self):
        data = block(seq=0) + block(crc=0xDEADBEEF, seq=1) + block(seq=2) + block(seq=0)
        warnings = []
        header, records, used = profile_report.sum_blocks(data, warnings.append)
        self.assertEqual(used, 3)
        self.assertEqual(header["unmatched"], "3")
        self.assertIn((0xC0101000, 9, 2700, 1800), records)
        self.assertTrue(any("CRC mismatch" in w for w in warnings))
        self.assertTrue(any("missing blocks" in w for w in warnings))
        self.assertTrue(any("reboot" in w for w in warnings))
        rc, out, _ = self.run_report(data)
        self.assertEqual(rc, 0)
        self.assertIn("blocks=3", out)

    def test_count_mismatch_and_missing(self):
        rc, _, err = self.run_report(block(count=3))
        self.assertEqual(rc, 1)
        self.assertIn("count mismatch", err)
        rc, _, err = self.run_report(b"no profile here\r\n")
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
