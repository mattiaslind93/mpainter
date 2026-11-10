#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard the real workspace save path against duplicate full serialization."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "scripts/host"))
from main_source import main_source
source = main_source(root)
manual = source.split("const auto save_document=", 1)[1].split("const auto open_document=", 1)[0]
assert "checkpoint(" not in manual, "Ctrl+S must not serialize recovery before the target"
assert manual.count("host.save_project(") == 1, "Exactly one persistence request per manual save"
print("Manual save schedules one persistence request without a recovery pre-save: PASS")
