#!/usr/bin/env python3
"""Offline tests for build-grub-direct.py --prepare-toolchain helpers."""
import importlib.util
import io
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "build_grub_direct", ROOT / "Tools/build-grub-direct.py"
)
grub = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(grub)


def recipe(order, sums):
    return (
        "pkgver=2.14\npkgrel=0\nsource=\"https://x/grub-2.14.tar.xz\n\t"
        + "\n\t".join(order) + "\n\t\"\n"
        "sha512sums=\"\n"
        + "\n".join(f"{'a' * 128}  {name}" for name in sums) + "\n\"\n"
    )


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_stanza_insertion(self):
        definition = self.root / "Makefile.core.def"
        definition.write_text("module = {\n  name = normal;\n};\n")
        grub.insert_stanza(definition)
        text = definition.read_text()
        start = text.find("name = undos_direct;")
        end = text.find("\n};", start)
        self.assertGreaterEqual(start, 0)
        self.assertGreater(end, start)
        for source in ("grub_adapter.c", "core/loader_core.c", "core/cpu_profile.c",
                       "core/diagnostics.c", "transition.S", "cpu_preflight.S"):
            self.assertIn(f"undos_direct/{source}", text[start:end])
        with self.assertRaises(ValueError):
            grub.insert_stanza(definition)

    def test_patch_order_follows_source(self):
        patches = grub.apkbuild_patches(recipe(["b.patch", "a.patch"], ["a.patch", "b.patch"]))
        self.assertEqual([name for name, _ in patches], ["b.patch", "a.patch"])
        with self.assertRaises(ValueError):
            grub.apkbuild_patches(recipe(["a.patch"], ["a.patch", "b.patch"]))

    def test_download_mismatch_removes_part(self):
        target = self.root / "file.bin"
        with self.assertRaisesRegex(ValueError, "file.bin"):
            grub.download("https://example.invalid/file.bin", target, "sha256",
                          "0" * 64, opener=lambda url: io.BytesIO(b"wrong"))
        self.assertFalse(target.exists())
        self.assertFalse((self.root / "file.bin.part").exists())
        with self.assertRaises(ValueError):
            grub.download("http://example.invalid/x", target, "sha256", "0" * 64,
                          opener=lambda url: io.BytesIO(b""))

    def test_provider_line_parses(self):
        line = "relocator.mod:00000120 T grub_relocator_prepare_relocs"
        match = re.match(r"(.+\.mod):[0-9a-fA-F]+\s+\w\s+(\S+)$", line)
        self.assertEqual(Path(match.group(1)).stem, "relocator")
        self.assertEqual(match.group(2), "grub_relocator_prepare_relocs")

    def test_tree_states(self):
        self.assertEqual(grub.tree_state(self.root)[0], "absent")
        (self.root / "source").mkdir()
        self.assertEqual(grub.tree_state(self.root), ("partial", ["source"]))
        for name in grub.TREE_PARTS:
            (self.root / name).mkdir(exist_ok=True)
        original = grub.is_tree_complete
        grub.is_tree_complete = lambda root: True
        try:
            self.assertEqual(grub.tree_state(self.root)[0], "complete")
        finally:
            grub.is_tree_complete = original

    def test_unmarked_staging_is_kept(self):
        staging = self.root / "staging"
        staging.mkdir()
        with self.assertRaises(ValueError):
            grub.remove_staging(staging)
        self.assertTrue(staging.is_dir())
        (staging / grub.STAGING_MARKER).write_text("")
        grub.remove_staging(staging)
        self.assertFalse(staging.exists())

    def test_missing_hint(self):
        path = grub.BUILD_ROOT / "target/grub-core/Makefile-does-not-exist"
        self.assertIn("--prepare-toolchain", grub.missing_tree_hint(path))
        self.assertEqual(grub.missing_tree_hint(self.root / "x"), "")

    def test_moddep_line_appended(self):
        shipped = self.root / "shipped.lst"
        shipped.write_text("boot: \nrelocator: mmap")
        target = self.root / "moddep.lst"
        grub.write_moddep(shipped, target)
        self.assertEqual(target.read_text(), "boot: \nrelocator: mmap\n"
                         "undos_direct: boot mmap relocator gzio\n")
        shipped.write_text("undos_direct: boot\n")
        with self.assertRaises(ValueError):
            grub.write_moddep(shipped, target)

    def test_completeness_requires_moddep_line(self):
        original = grub.verify_pinned_inputs
        grub.verify_pinned_inputs = lambda root: {}
        try:
            for path in grub.required_tree_files(self.root):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("")
            definition = self.root / "source/grub-core/Makefile.core.def"
            definition.parent.mkdir(parents=True, exist_ok=True)
            definition.write_text("name = undos_direct;\n")
            self.assertFalse(grub.is_tree_complete(self.root))
            (self.root / "target/grub-core/moddep.lst").write_text(
                grub.MODULE_DEPENDENCY_LINE + "\n")
            self.assertTrue(grub.is_tree_complete(self.root))
        finally:
            grub.verify_pinned_inputs = original

    def test_toolchain_recipe_unchanged(self):
        self.assertEqual(grub.TOOLCHAIN_IMAGE, "undos-grub-toolchain:fe271a28e34418af")
        self.assertNotEqual(grub.PREPARE_IMAGE, grub.TOOLCHAIN_IMAGE)


if __name__ == "__main__":
    unittest.main()
