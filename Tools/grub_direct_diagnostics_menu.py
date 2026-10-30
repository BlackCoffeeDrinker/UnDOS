"""Render the opt-in manual menu for direct-loader VGA stop diagnostics."""

import re


STOP_NAMES = (
    "command",
    "loaded",
    "callback",
    "profile",
    "prepared",
    "copied",
    "parameters",
    "segments",
    "cleanup",
    "paging",
    "frame",
    "jump",
)


def render_diagnostic_submenu(build_id, root_uuid, kernel_build_id=None):
    if not re.fullmatch(r"[0-9a-f]{8}", build_id):
        raise ValueError("Diagnostic menu build ID must be eight lowercase hex digits")
    if not re.fullmatch(r"[0-9A-Fa-f-]{9}", root_uuid):
        raise ValueError("Diagnostic menu root UUID is invalid")
    if kernel_build_id is not None and not re.fullmatch(
            r"[0-9a-f]{8}", kernel_build_id):
        raise ValueError("Kernel diagnostic build ID must be eight lowercase hex digits")

    lines = [
        f'submenu "UnDOS - handoff STOP diagnostics ({build_id})" {{',
        "    terminal_input console",
        "    terminal_output console",
    ]

    def append_entry(label, command, kernel_path):
        lines.extend(
            (
                f'    menuentry "{label} (deliberate hold; photograph, then reset)" {{',
                '        echo "Manual diagnostic only: expect a deliberate VGA hold."',
                '        echo "Photograph the named hold, then reset or power-cycle."',
                "        echo 'Press Enter to continue to the VGA visibility check.'",
                "        read udl_diag_confirm",
                "        set udl_diag_prepared=0",
                "        insmod /boot/grub/i386-pc/undos_direct.mod",
                "        undos_direct_diag "
                f"{command} {kernel_path} "
                f"/boot/direct/hal_x86_old.elf -- CMDLINE=12 root=UUID={root_uuid}",
                '        if test "$udl_diag_prepared" = "1"; then',
                "            unset udl_diag_prepared",
                "            boot",
                '            echo "Boot returned after the selected diagnostic."',
                "        else",
                '            echo "Diagnostic module or preparation failed; boot was not attempted."',
                "        fi",
                "        unset udl_diag_prepared",
                '        echo "Press Enter to return to the GRUB menu."',
                "        read udl_diag_return",
                "    }",
            )
        )

    for stop in STOP_NAMES:
        append_entry(
            f"STOP {stop}", stop, "/boot/direct/kernel.elf"
        )
    if kernel_build_id is not None:
        append_entry(
            f"STOP kernel ({kernel_build_id})",
            "kernel",
            "/boot/direct-diag/kernel-entry-stop.elf",
        )
    lines.append("}")
    return "\n".join(lines) + "\n"
