#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Report stylesheet sizes that are not on the scale in docs/design/ui.md.

Colors already have check-theme-colors.py. This is the same idea for the rest:
type sizes, control heights and spacing drift apart one rule at a time, and
nothing notices until the panels no longer line up.

Run with --strict to fail on a violation. Without it, it prints what it found
and exits 0, which is what you want while the existing rules are still being
brought onto the scale.
"""
import argparse
import pathlib
import re
import sys

# docs/design/ui.md §2.2, §5.1, §6.1, §6.5.
TYPE_SIZES = {10, 11, 13}
SPACING = {0, 2, 4, 6, 8, 12, 16}
CONTROL_HEIGHTS = {16, 20, 28}
# A list row has its own rhythm: it carries a row of text, not a control.
ROW_HEIGHTS = {18, 22, 26}
RADIUS = {3}

# Sizes that are not control heights and are exempt by role, with the reason.
HEIGHT_EXEMPT = {
    2: 'separator rule',
    1: 'hairline',
    7: 'scrollbar groove',
    8: 'scrollbar groove',
    32: 'splash and progress bands',
}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--strict', action='store_true')
args = parser.parse_args()

source = (pathlib.Path(__file__).resolve().parents[2] / 'native/host/ui/theme/Theme.cc').read_text()

# One rule per entry, so a violation can be named by the selector it sits in.
findings = []
for block in re.finditer(r'([^{};]+)\{([^}]*)\}', source):
    selector = ' '.join(block.group(1).split())[-80:]
    body = block.group(2)
    for name, value in re.findall(r'([a-z-]+)\s*:\s*([^;]+);', body):
        numbers = [int(n) for n in re.findall(r'(\d+)px', value)]
        if not numbers:
            continue
        if name == 'font-size':
            bad = [n for n in numbers if n not in TYPE_SIZES]
            if bad:
                findings.append((selector, name, value.strip(), 'not a type size'))
        elif name in ('padding', 'margin') or name.startswith(('padding-', 'margin-')):
            # A handle overhangs its groove on purpose, with a negative margin.
            if 'handle' in selector:
                continue
            bad = [n for n in numbers if n not in SPACING]
            if bad:
                findings.append((selector, name, value.strip(), 'not on the spacing scale'))
        elif name in ('min-height', 'height', 'max-height'):
            allowed = ROW_HEIGHTS if '::item' in selector else CONTROL_HEIGHTS
            bad = [n for n in numbers if n not in allowed and n not in HEIGHT_EXEMPT]
            if bad:
                what = 'not a row height' if '::item' in selector else 'not a control height'
                findings.append((selector, name, value.strip(), what))
        elif name == 'border-radius':
            # A circle is half its own fixed size and is not the shared radius.
            if 'handle' in selector or 'indicator' in selector:
                continue
            bad = [n for n in numbers if n not in RADIUS]
            if bad:
                findings.append((selector, name, value.strip(), 'not the control radius'))

if not findings:
    print('UI metrics audit passed: every stylesheet size is on the scale.')
    sys.exit(0)

print(f'{len(findings)} stylesheet values are off the scale in docs/design/ui.md:')
for selector, name, value, why in findings:
    print(f'  {name}: {value:<14} {why:<28} {selector}')
sys.exit(1 if args.strict else 0)
