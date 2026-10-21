"""Regression coverage for the registry editor's tree navigation and value edits."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "configuration_editor"))

import cfg_regedit
from hive_api import Config


class RegistryEditorTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="undos-regedit-")
        self.addCleanup(temporary.cleanup)
        self.filename = str(Path(temporary.name) / "test.hive")
        config = Config(self.filename)
        config.set("/A/Child", "hidden")
        for index in range(30):
            config.set(f"/B/Value{index:02}", f"value {index}")
        config.save()

        self.screen = MagicMock()
        self.screen.getmaxyx.return_value = (15, 120)
        self.screen.getstr.return_value = b"updated"
        curses = patch.multiple(
            cfg_regedit.curses,
            start_color=MagicMock(),
            use_default_colors=MagicMock(),
            init_pair=MagicMock(),
            color_pair=MagicMock(return_value=1),
            echo=MagicMock(),
            noecho=MagicMock(),
            curs_set=MagicMock(),
            ACS_VLINE=ord("|"),
            create=True,
        )
        curses.start()
        self.addCleanup(curses.stop)
        self.app = cfg_regedit.RegEditApp(self.screen, self.filename)

    def select(self, path):
        self.app.tree_selected_idx = next(
            index for index, (node, _) in enumerate(self.app.tree_flat)
            if node.path == path
        )

    def tree_rows(self):
        return [
            call.args for call in self.screen.addstr.call_args_list
            if call.args[1] == 1
        ]

    def test_keyboard_navigation_scrolls_down_and_back_up(self):
        count = len(self.app.tree_flat)
        self.screen.getch.side_effect = (
            [cfg_regedit.curses.KEY_DOWN] * (count - 1)
            + [cfg_regedit.curses.KEY_UP] * (count - 1)
            + [ord("q")]
        )
        self.app.run()
        self.assertTrue(any("Value29" in row[2] for row in self.tree_rows()))
        self.assertEqual(self.app.tree_selected_idx, 0)
        self.assertEqual(self.app.tree_scroll_offset, 0)

    def test_viewport_keeps_selection_visible_after_resize(self):
        self.select("/B/Value20")
        for height in (15, 10, 25):
            with self.subTest(height=height):
                self.screen.getmaxyx.return_value = (height, 120)
                self.screen.addstr.reset_mock()
                self.app.draw()
                rows = self.tree_rows()
                selected_row = next(row for row in rows if "Value20" in row[2])
                self.assertGreaterEqual(selected_row[0], 3)
                self.assertLess(selected_row[0], height - 2)
                self.assertEqual(len(rows), height - 5)

    def test_edit_preserves_collapsed_nodes_selection_and_viewport(self):
        self.app.root_node.children[0].expanded = False
        self.app.refresh_flat_tree()
        self.select("/B/Value20")
        self.app.draw()
        selected_idx = self.app.tree_selected_idx
        offset = self.app.tree_scroll_offset
        node = self.app.tree_flat[selected_idx][0]

        self.app.edit_selected_value()
        self.app.draw()

        self.assertFalse(self.app.root_node.children[0].expanded)
        self.assertEqual(self.app.tree_selected_idx, selected_idx)
        self.assertEqual(self.app.tree_scroll_offset, offset)
        self.assertIs(self.app.tree_flat[selected_idx][0], node)
        self.assertEqual(node.entry.value, "updated")
        self.assertEqual(self.app.entries[node.path].value, "updated")
        self.assertEqual(Config(self.filename).entries[node.path], (Config.VALUE_STRING, "updated"))

    def test_collapse_clamps_viewport_to_shorter_tree(self):
        self.select("/B/Value29")
        self.app.draw()
        self.select("/B")
        self.app.tree_flat[self.app.tree_selected_idx][0].expanded = False
        self.app.refresh_flat_tree()
        self.screen.addstr.reset_mock()
        self.app.draw()
        self.assertEqual(self.app.tree_scroll_offset, 0)
        self.assertEqual(len(self.tree_rows()), len(self.app.tree_flat))

    def test_empty_tree_resets_viewport(self):
        self.app.entries.clear()
        self.app.rebuild_tree()
        self.app.tree_scroll_offset = 10
        self.app.draw()
        self.assertEqual(self.app.tree_selected_idx, 0)
        self.assertEqual(self.app.tree_scroll_offset, 0)
        self.assertEqual(self.tree_rows(), [])

    def test_edit_reports_unnamed_hive_error_without_changing_value(self):
        self.select("/B/Value20")
        node = self.app.tree_flat[self.app.tree_selected_idx][0]
        original = node.entry.value
        self.app.config._unnamed_paths.add(node.path)
        self.app.edit_selected_value()
        self.assertIn("unresolved key names", self.app.status_msg)
        self.assertEqual(node.entry.value, original)
        self.assertEqual(self.app.config.get(node.path), original)
        self.assertEqual(Config(self.filename).get(node.path), original)


if __name__ == "__main__":
    unittest.main()
