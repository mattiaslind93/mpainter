# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep legacy verification commands on the caller's private Xvfb display."""
import os
from pathlib import Path

def private_display():
    display = os.environ.get("DISPLAY", "")
    number = display.removeprefix(":").split(".")[0]
    if os.environ.get("WAYLAND_DISPLAY") or not display.startswith(":") or not number.isdigit() or int(number) < 2:
        raise RuntimeError("Run inside bts-build with env -u WAYLAND_DISPLAY xvfb-run -a")
    try:
        pid = Path(f"/tmp/.X{number}-lock").read_text().strip()
        if Path(f"/proc/{pid}/comm").read_text().strip() != "Xvfb":
            raise ValueError("not Xvfb")
    except (OSError, ValueError) as error:
        raise RuntimeError("Verification requires a private Xvfb display") from error
    return display
