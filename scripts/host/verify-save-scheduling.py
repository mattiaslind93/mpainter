#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard the real workspace save path against duplicate full serialization."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "scripts/host"))
from main_source import compact, main_source
# Whitespace-free, so the check holds however the session is formatted.
source = compact(main_source(root))
manual = source.split("constautosave_document=", 1)[1].split("constautoopen_document=", 1)[0]
assert "checkpoint(" not in manual, "Ctrl+S must not serialize recovery before the target"
assert manual.count("host.save_project(") == 1, "Exactly one persistence request per manual save"
print("Manual save schedules one persistence request without a recovery pre-save: PASS")
