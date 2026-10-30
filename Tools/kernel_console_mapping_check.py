"""Paused-guest CR3 switch test; no guest code or process creation is executed."""

import re
import json
import os
import socket
import struct
import xml.etree.ElementTree as ET


class Remote:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(2)
        self.socket.connect(str(path))

    def command(self, command):
        raw = command.encode()
        self.socket.sendall(b"$" + raw + b"#" + f"{sum(raw) % 256:02x}".encode())
        while self.socket.recv(1) != b"$":
            pass
        data = bytearray()
        while True:
            c = self.socket.recv(1)
            if c == b"#":
                break
            data.extend(c)
        checksum = self.socket.recv(2)
        if int(checksum, 16) != sum(data) % 256:
            raise RuntimeError("GDB checksum mismatch")
        self.socket.sendall(b"+")
        return data.decode()

    def xml(self, name):
        offset, text = 0, ""
        while True:
            part = self.command(f"qXfer:features:read:{name}:{offset:x},800")
            text += part[1:]
            if part[0] == "l":
                return ET.fromstring(text.replace("xi:", ""))
            if part[0] != "m":
                raise RuntimeError("GDB target XML unavailable")
            offset += len(part) - 1

    def read(self, address, size):
        return "".join(self.command(f"m{address + offset:x},{min(512, size-offset):x}")
                       for offset in range(0, size, 512))

    def write(self, address, data):
        for offset in range(0, len(data), 1024):
            part = data[offset:offset + 1024]
            if self.command(f"M{address + offset//2:x},{len(part)//2:x}:{part}") != "OK":
                raise RuntimeError("Cannot write paused guest memory")


def verify(monitor, output, root):
    monitor.command("stop")
    registers = monitor.command("human-monitor-command", {"command-line": "info registers"})
    (output / "registers.txt").write_text(registers)
    cr3 = int(re.search(r"CR3=([0-9a-fA-F]+)", registers).group(1), 16)
    pd_file = output / "page-directory.bin"
    monitor.command("pmemsave", {"val": cr3, "size": 4096, "filename": str(pd_file)})
    pd = list(struct.unpack("<1024I", pd_file.read_bytes()))
    alias = None
    for index in range(768, 1022):
        if not pd[index] & 1:
            continue
        table_file = output / "page-table.bin"
        monitor.command("pmemsave", {"val": pd[index] & 0xfffff000,
                                    "size": 4096, "filename": str(table_file)})
        for entry_index, entry in enumerate(struct.unpack("<1024I", table_file.read_bytes())):
            if entry & 0xfffff001 == 0xb8001:
                if entry & 4 or entry & 0x18 != 0x18:
                    raise RuntimeError("VGA alias is user-accessible or cached")
                alias = (index << 22) | (entry_index << 12)
                break
        if alias is not None:
            break
    if alias is None:
        raise RuntimeError("No supervisor non-cached persistent VGA alias")
    socket_path = root / "build" / f"q-console-gdb-{os.getpid()}"
    response = monitor.command("human-monitor-command", {
        "command-line": f"gdbserver unix:{socket_path},server=on,wait=off"})
    (output / "gdbserver.txt").write_text(response)
    remote = Remote(socket_path)
    scratch = 0x1f00000
    saved = None
    changed_root = False
    try:
        remote.command("qSupported:xmlRegisters=i386")
        target = remote.xml("target.xml")
        features = [target]
        for item in target.iter():
            if item.tag.endswith("include"):
                features.append(remote.xml(item.attrib["href"]))
        regnum, cr3_register = 0, None
        for feature in features:
            for reg in feature.iter("reg"):
                regnum = int(reg.attrib.get("regnum", regnum))
                if reg.attrib["name"] == "cr3":
                    cr3_register = regnum
                regnum += 1
        if cr3_register is None:
            raise RuntimeError("GDB does not expose CR3")
        if remote.command("Qqemu.PhyMemMode:1") != "OK":
            raise RuntimeError("Physical debug memory mode unavailable")
        saved = remote.read(scratch, 4096)
        if len(saved) != 8192:
            raise RuntimeError("Cannot preserve paused guest scratch RAM")
        clone = pd[:]
        clone[:768] = [0] * 768
        clone[1022] = 0
        clone[1023] = scratch | 3
        data = struct.pack("<1024I", *clone).hex()
        remote.write(scratch, data)
        if remote.command(f"P{cr3_register:x}={struct.pack('<I', scratch).hex()}") != "OK":
            raise RuntimeError("Cannot switch paused guest CR3")
        changed_root = True
        remote.command("Qqemu.PhyMemMode:0")
        actual = remote.read(alias, 4000)
        expected = (output / "vga.bin").read_bytes().hex()
        if actual != expected:
            raise RuntimeError("Persistent VGA alias changed with low mappings absent")
        (output / "mapping-result.json").write_text(
            json.dumps({"status": "PASS", "original_cr3": cr3,
                                     "temporary_cr3": scratch, "vga_alias": alias,
                                     "low_pdes_cleared": 768, "guest_executed": False}, indent=2) + "\n")
    finally:
        if changed_root:
            if remote.command(f"P{cr3_register:x}={struct.pack('<I', cr3).hex()}") != "OK":
                raise RuntimeError("Cannot restore original paused CR3")
        if saved is not None:
            remote.command("Qqemu.PhyMemMode:1")
            remote.write(scratch, saved)
            if remote.read(scratch, 4096) != saved:
                raise RuntimeError("Paused scratch RAM restoration failed")
            remote.command("Qqemu.PhyMemMode:0")
        remote.socket.close()
        socket_path.unlink(missing_ok=True)
        monitor.command("cont")
