#!/usr/bin/env python3
"""
CFG1 Hive Registry Editor (RegEdit TUI)
Compatible with kernel configuration_manager.cpp

The tree scrolls with the selection; value edits preserve navigation state.
"""

import curses
import sys
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from hive_api import Config, key_hash


# --- Hive Data Structures ---
@dataclass
class HiveEntry:
    key_hash: int
    parent_hash: int
    name: str
    value_type: int
    value: Any


class RegistryNode:
    def __init__(self, entry: Optional[HiveEntry] = None, path: str = "/"):
        self.entry = entry
        self.path = path
        self.children: List['RegistryNode'] = []
        self.expanded: bool = True

    @property
    def name(self) -> str:
        return self.entry.name if self.entry else "ROOT"

    @property
    def hash(self) -> int:
        return self.entry.key_hash if self.entry else 0


# --- Curses RegEdit App ---
class RegEditApp:
    def __init__(self, stdscr, filename: str):
        self.stdscr = stdscr
        self.filename = filename
        self.config = Config(filename)
        self.entries: Dict[str, HiveEntry] = {}
        self.root_node = RegistryNode(None)

        # UI State
        self.active_pane = 0  # 0: Tree (Left), 1: Values (Right)
        self.tree_flat: List[Tuple[RegistryNode, int]] = []  # (node, depth)
        self.tree_selected_idx = 0
        self.tree_scroll_offset = 0
        self.val_selected_idx = 0
        self.status_msg = f"Loaded: {filename} ({len(self.config.entries)} entries)"

        # Color Pairs
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLUE)  # Header / Active
        curses.init_pair(2, curses.COLOR_CYAN, -1)  # Keys
        curses.init_pair(3, curses.COLOR_YELLOW, -1)  # Values

        self.load_from_config()
        self.rebuild_tree()

    def load_from_config(self):
        """Load real hive entries from the Config API into the internal store.

        Entries are keyed by their full path rather than by keyHash alone,
        since keyHash only depends on the short name and can collide between
        sibling entries under different parents (e.g. two drivers both
        having an "ImagePath" value).
        """
        self.entries = {}
        for path, (value_type, value) in self.config.entries.items():
            short = Config._short_name(path)
            parent_path = Config._parent_path(path)
            parent_hash = 0 if parent_path == "/" else key_hash(Config._short_name(parent_path))
            khash = key_hash(short)
            self.entries[path] = HiveEntry(khash, parent_hash, short, value_type, value)

    def rebuild_tree(self):
        """Build hierarchical node tree from parent-path references."""
        nodes: Dict[str, RegistryNode] = {"/": self.root_node}
        self.root_node.children.clear()

        for path, entry in self.entries.items():
            nodes[path] = RegistryNode(entry, path)

        for path, entry in self.entries.items():
            parent_path = Config._parent_path(path)
            parent = nodes.get(parent_path, self.root_node)
            parent.children.append(nodes[path])

        for node in nodes.values():
            node.children.sort(key=lambda n: n.name)

        self.refresh_flat_tree()

    def refresh_flat_tree(self):
        """Flatten tree for list navigation."""
        self.tree_flat = []

        def _walk(node: RegistryNode, depth: int):
            for child in node.children:
                self.tree_flat.append((child, depth))
                if child.expanded:
                    _walk(child, depth + 1)

        _walk(self.root_node, 0)
        if self.tree_selected_idx >= len(self.tree_flat):
            self.tree_selected_idx = max(0, len(self.tree_flat) - 1)

    def save(self):
        self.config.modified = True
        try:
            self.config.save(force=True)
        except ValueError as error:
            self.status_msg = str(error)
            return False
        self.status_msg = f"Saved: {self.filename}"
        return True

    def edit_selected_value(self):
        if not self.tree_flat:
            return
        node, _ = self.tree_flat[self.tree_selected_idx]
        if node.entry is None or node.entry.value_type == Config.VALUE_INVALID:
            self.status_msg = "Cannot edit a folder key"
            return

        curses.echo()
        curses.curs_set(1)
        max_y, max_x = self.stdscr.getmaxyx()
        prompt = f"New value for {node.path}: "
        self.stdscr.addstr(max_y - 2, 2, prompt.ljust(max_x - 3))
        self.stdscr.refresh()
        try:
            raw = self.stdscr.getstr(max_y - 2, 2 + len(prompt), max_x - 4 - len(prompt))
            new_str = raw.decode("utf-8", errors="replace")
        finally:
            curses.noecho()
            curses.curs_set(0)

        value_type = node.entry.value_type
        try:
            if value_type == Config.VALUE_BOOLEAN:
                new_value: Any = new_str.strip().lower() in ("true", "1", "yes")
            elif value_type in (
                Config.VALUE_UINT32,
                Config.VALUE_UINT64,
                Config.VALUE_SINT32,
                Config.VALUE_SINT64,
            ):
                new_value = int(new_str.strip())
            else:
                new_value = new_str
        except ValueError:
            self.status_msg = f"Invalid value for type: {new_str!r}"
            return

        self.config.set(node.path, new_value, value_type)
        if not self.save():
            self.config.entries[node.path] = (value_type, node.entry.value)
            return
        node.entry.value = new_value

    def draw(self):
        self.stdscr.clear()
        max_y, max_x = self.stdscr.getmaxyx()
        split_x = max_x // 3

        # Header
        header = f" UnDOS RegEdit v1.0 — [{self.filename}] "
        self.stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
        self.stdscr.addstr(0, 0, header.ljust(max_x))
        self.stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)

        # Footer / Controls
        footer = " TAB: Switch Pane | ARROWS: Navigate | SPACE: Expand | E: Edit | S: Save | Q: Quit "
        self.stdscr.attron(curses.color_pair(1))
        self.stdscr.addstr(max_y - 1, 0, footer.ljust(max_x)[:max_x - 1])
        self.stdscr.attroff(curses.color_pair(1))

        # Vertical Divider
        for y in range(1, max_y - 1):
            self.stdscr.addch(y, split_x, curses.ACS_VLINE)

        # Pane Headers
        left_title = " HIVE TREE " if self.active_pane == 0 else " Hive Tree "
        right_title = " KEYS & VALUES " if self.active_pane == 1 else " Keys & Values "

        self.stdscr.addstr(1, 2, left_title, curses.A_BOLD)
        self.stdscr.addstr(1, split_x + 2, right_title, curses.A_BOLD)

        # Render Left Pane (Tree)
        tree_y_start = 3
        tree_height = max(0, max_y - tree_y_start - 2)
        self.tree_scroll_offset = min(
            self.tree_scroll_offset, max(0, len(self.tree_flat) - tree_height)
        )
        if tree_height:
            if self.tree_selected_idx < self.tree_scroll_offset:
                self.tree_scroll_offset = self.tree_selected_idx
            elif self.tree_selected_idx >= self.tree_scroll_offset + tree_height:
                self.tree_scroll_offset = self.tree_selected_idx - tree_height + 1

        visible_nodes = self.tree_flat[self.tree_scroll_offset:self.tree_scroll_offset + tree_height]
        for row, (node, depth) in enumerate(visible_nodes):
            idx = self.tree_scroll_offset + row
            y = tree_y_start + row

            prefix = "  " * depth + ("[-] " if node.children and node.expanded else "[+] " if node.children else "    ")
            label = f"{prefix}{node.name}"[:split_x - 1]

            if idx == self.tree_selected_idx:
                attr = curses.color_pair(1) if self.active_pane == 0 else curses.A_REVERSE
                self.stdscr.addstr(y, 1, label.ljust(split_x - 1), attr)
            else:
                self.stdscr.addstr(y, 1, label, curses.color_pair(2))

        # Render Right Pane (Selected Node Data)
        if self.tree_flat:
            cur_node, _ = self.tree_flat[self.tree_selected_idx]
            val_y = 3

            # Line 1: Selected Key Details
            info = f"Key Hash: 0x{cur_node.hash:016X} | Type: {cur_node.entry.value_type if cur_node.entry else 0}"
            self.stdscr.addstr(val_y, split_x + 2, info[:max_x - split_x - 3], curses.A_DIM)
            val_y += 1

            path_line = f"Path        : {cur_node.path}"
            self.stdscr.addstr(val_y, split_x + 2, path_line[:max_x - split_x - 3], curses.A_DIM)
            val_y += 2

            # Line 2: Payload Preview
            if cur_node.entry is None:
                payload = "<ROOT>"
            elif cur_node.entry.value_type == Config.VALUE_INVALID:
                payload = "<folder>"
            else:
                payload = str(cur_node.entry.value)
            self.stdscr.addstr(val_y, split_x + 2, f"Value Data : {payload}"[:max_x - split_x - 3], curses.color_pair(3))

        # Status Line
        self.stdscr.addstr(max_y - 2, 2, f"Status: {self.status_msg}"[:max_x - 3], curses.A_DIM)
        self.stdscr.refresh()

    def run(self):
        while True:
            self.draw()
            ch = self.stdscr.getch()

            if ch in (ord('q'), ord('Q')):
                break
            elif ch == 9:  # TAB key
                self.active_pane = 1 - self.active_pane
            elif ch == curses.KEY_UP:
                if self.active_pane == 0 and self.tree_selected_idx > 0:
                    self.tree_selected_idx -= 1
            elif ch == curses.KEY_DOWN:
                if self.active_pane == 0 and self.tree_selected_idx < len(self.tree_flat) - 1:
                    self.tree_selected_idx += 1
            elif ch == ord(' '):  # Expand / Collapse Node
                if self.tree_flat:
                    node, _ = self.tree_flat[self.tree_selected_idx]
                    node.expanded = not node.expanded
                    self.refresh_flat_tree()
            elif ch in (ord('e'), ord('E')):
                self.edit_selected_value()
            elif ch in (ord('s'), ord('S')):
                self.save()


def main(stdscr):
    filename = sys.argv[1] if len(sys.argv) > 1 else "localmachine.hive"
    app = RegEditApp(stdscr, filename)
    app.run()


if __name__ == "__main__":
    curses.wrapper(main)
