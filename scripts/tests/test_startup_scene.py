# SPDX-License-Identifier: GPL-3.0-or-later
"""A customer launch opens on the empty workspace, not on the Shaderball test scene.

The session may open the bundled Shaderball at startup only when the harness option
--startup-shaderball is set. A release build replaces the harness with HarnessDisabled.hh, whose
is_set() is always false, so that build can never open it.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SESSION = ROOT / "native/host/app/WorkspaceSession.inc"
OPTIONS = ROOT / "native/harness/HarnessOptions.hh"
DISABLED = ROOT / "native/host/app/HarnessDisabled.hh"


def compact(text):
    return re.sub(r"\s+", "", text)


class StartupSceneTest(unittest.TestCase):
    def test_startup_shaderball_needs_the_harness_option(self):
        session = compact(SESSION.read_text())
        start = session.index("QTimerstartup_project;")
        click = session.index('"useShaderballMesh"', start)
        gate = session.find('if(harness.is_set("startup-shaderball")){', start)
        self.assertNotEqual(gate, -1, "startup Shaderball is gated on --startup-shaderball")
        self.assertLess(gate, click, "the gate comes before the Shaderball is chosen")
        self.assertEqual(session.count('"useShaderballMesh"'), 1,
                         "no other startup path chooses the Shaderball")

    def test_option_is_registered_by_the_harness_only(self):
        self.assertIn('{"startup-shaderball"', compact(OPTIONS.read_text()))
        self.assertNotIn("startup-shaderball", (ROOT / "native/host/app/main.cc").read_text())

    def test_release_build_never_sets_harness_options(self):
        disabled = compact(DISABLED.read_text())
        self.assertIn("boolis_set(constQString&)const{returnfalse;}", disabled)


if __name__ == "__main__":
    unittest.main()
