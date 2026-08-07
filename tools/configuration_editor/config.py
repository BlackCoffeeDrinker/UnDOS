#!/usr/bin/env python3
"""Author and inspect CFG1 configuration hive files matching the kernel layout.

The on-disk format and the Config class that reads/writes it now live in
hive_api.py, so this module (and other tools such as cfg_regedit.py) can
share a single implementation.
"""

from __future__ import annotations

import os
import sys
from typing import Any

from hive_api import Config


def create_database(filename: str) -> None:
    cfg = Config(filename)
    cfg.entries = {}
    cfg.modified = True
    cfg.save(force=True)


def author_localmachine_hive(filename: str) -> None:
    """Write the boot LocalMachine hive with the boot-start and automatic-start drivers."""
    cfg = Config(filename)
    cfg.entries = {}

    # Boot-start: needed to bring up the disk stack and mount the root fs,
    # so their ImagePath is resolved through "/" while it is still the
    # early-boot BootDeviceVolume mount, before it gets replaced by the
    # real root filesystem.
    boot_drivers = [
        ("bus_isa", "/System/Drivers/bus_isa.elf"),
        ("disk_ata", "/System/Drivers/disk_ata.elf"),
        ("volmgr", "/System/Drivers/volmgr.elf"),
        ("fs_fat", "/System/Drivers/fs_fat.elf"),
    ]

    # Automatic-start: loaded only after the real root fs is mounted and
    # LocalMachine has been rebound onto it, so ImagePath is a normal,
    # post-mount root path.
    automatic_drivers = [
        ("gfx_vga", "/System/Drivers/gfx_vga.elf"),
    ]

    cfg.ensure_key("/Drivers")
    for name, image in boot_drivers:
        cfg.ensure_key(f"/Drivers/{name}")
        cfg.set(f"/Drivers/{name}/Start", "Boot", Config.VALUE_STRING)
        cfg.set(f"/Drivers/{name}/ImagePath", image, Config.VALUE_STRING)

    for name, image in automatic_drivers:
        cfg.ensure_key(f"/Drivers/{name}")
        cfg.set(f"/Drivers/{name}/Start", "Automatic", Config.VALUE_STRING)
        cfg.set(f"/Drivers/{name}/ImagePath", image, Config.VALUE_STRING)

    # LegacyISAReservations: "the registry is the enumeration" for fixed,
    # always-present ISA hardware that no real bus discovers on its own. The
    # kernel's generic legacy enumerator (EnumerateLegacyISADevices) walks
    # this subtree at boot, probes each entry, and reports a PDO for every
    # one detected. DriverImage is left unset for gfx_vga since it is already
    # loaded above as an Automatic-start driver; a device with no matching
    # loaded driver simply reports a PDO that stays unmatched.
    legacy_isa_devices = [
        ("VGA", {"HwID": "gfx_vga", "IoPorts": "0x3C0-0x3DF", "Memory": "0xA0000-0xAFFFF"}),
    ]

    cfg.ensure_key("/LegacyISAReservations")
    for name, values in legacy_isa_devices:
        cfg.ensure_key(f"/LegacyISAReservations/{name}")
        for value_name, value in values.items():
            cfg.set(f"/LegacyISAReservations/{name}/{value_name}", value, Config.VALUE_STRING)

    cfg.save(force=True)
    print(f"Authored LocalMachine hive: {filename} ({len(cfg.entries)} entries)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python config.py <path-to-config> <command> [args...]")
        print("Commands: create, edit, delete, list, vacuum, ensure, author-localmachine")
        sys.exit(1)

    filename = sys.argv[1]
    command = sys.argv[2]

    if command == "author-localmachine":
        author_localmachine_hive(filename)
        sys.exit(0)

    if command == "create" and len(sys.argv) == 3:
        create_database(filename)
        print(f"Created configuration database: {filename}")
        sys.exit(0)

    if command == "create" and len(sys.argv) >= 5:
        # create <key> <value> [type] — create DB if needed, then set key
        if not os.path.exists(filename):
            create_database(filename)
        config = Config(filename)
        key = sys.argv[3]
        value_str = sys.argv[4]
        value_type = None
        if len(sys.argv) > 5:
            type_map = {
                "bool": Config.VALUE_BOOLEAN,
                "uint32": Config.VALUE_UINT32,
                "uint64": Config.VALUE_UINT64,
                "sint32": Config.VALUE_SINT32,
                "sint64": Config.VALUE_SINT64,
                "string": Config.VALUE_STRING,
                "key": Config.VALUE_INVALID,
            }
            value_type = type_map.get(sys.argv[5])
        if value_type == Config.VALUE_INVALID:
            config.ensure_key(key)
        else:
            if value_type == Config.VALUE_BOOLEAN or (
                value_type is None and value_str.lower() in ("true", "false")
            ):
                value: Any = value_str.lower() == "true"
            elif value_type in (
                Config.VALUE_UINT32,
                Config.VALUE_UINT64,
                Config.VALUE_SINT32,
                Config.VALUE_SINT64,
            ) or (value_type is None and value_str.lstrip("-").isdigit()):
                value = int(value_str)
            else:
                value = value_str
            config.set(key, value, value_type)
        config.save()
        print(f"Created entry: {key}")
        sys.exit(0)

    if not os.path.exists(filename):
        print(f"Error: Configuration file '{filename}' does not exist. Use 'create' first.")
        sys.exit(1)

    config = Config(filename)

    if command == "edit":
        if len(sys.argv) < 5:
            print("Usage: python config.py <path-to-config> edit <key> <value>")
            sys.exit(1)
        key = sys.argv[3]
        value_str = sys.argv[4]
        existing = config.entries.get(config._normalize_path(key))
        if existing:
            value_type = existing[0]
            if value_type == Config.VALUE_BOOLEAN:
                value = value_str.lower() == "true"
            elif value_type in (
                Config.VALUE_UINT32,
                Config.VALUE_UINT64,
                Config.VALUE_SINT32,
                Config.VALUE_SINT64,
            ):
                value = int(value_str)
            else:
                value = value_str
        else:
            if value_str.lower() in ("true", "false"):
                value = value_str.lower() == "true"
            elif value_str.lstrip("-").isdigit():
                value = int(value_str)
            else:
                value = value_str
            value_type = None
        config.set(key, value, value_type)
        config.save()
        print(f"Updated entry: {key} = {value}")

    elif command == "ensure":
        if len(sys.argv) < 4:
            print("Usage: python config.py <path-to-config> ensure <key>")
            sys.exit(1)
        config.ensure_key(sys.argv[3])
        config.save()
        print(f"Ensured key: {sys.argv[3]}")

    elif command == "delete":
        if len(sys.argv) < 4:
            print("Usage: python config.py <path-to-config> delete <key>")
            sys.exit(1)
        config.delete(sys.argv[3])
        config.save()
        print(f"Deleted entry: {sys.argv[3]}")

    elif command == "list":
        entries = config.list_all()
        if not entries:
            print("No entries found.")
        else:
            for key in sorted(entries.keys(), key=lambda p: (p.count("/"), p)):
                value = entries[key]
                vtype = config.entries[key][0]
                if vtype == Config.VALUE_INVALID:
                    print(f"{key}/")
                else:
                    print(f"{key} = {value!r}")

    elif command == "vacuum":
        config.modified = True
        config.save(force=True)
        print(f"Vacuumed configuration database: {filename}")

    else:
        print(f"Unknown command: {command}")
        print("Available commands: create, edit, delete, list, vacuum, ensure, author-localmachine")
        sys.exit(1)
