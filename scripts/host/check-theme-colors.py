#!/usr/bin/env python3
"""Reject new literal UI hex colors outside the shared theme.

Diagnostic pixel fixtures, material/channel data, and branding vector assets are
not interface colors. QColor/fromRgb calls with computed data remain valid.
"""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
failures = []
for path in (root / 'native/host').iterdir():
    if path.suffix not in ('.cc', '.hh') or path.name == 'Theme.hh':
        continue
    if 'Demo' in path.name or 'Test' in path.name:
        continue
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if re.search(r'Qt::(?:gray|darkGray|lightGray)\b|#[0-9a-fA-F]{3,8}\b|\brgba?\(\s*\d+\s*,|(?:color|background)\s*:\s*(?:white|black|gr[ae]y)\b', line):
            failures.append(f'{path.relative_to(root)}:{number}: {line[:160]}')
if failures:
    raise SystemExit('\n'.join(failures))
print('UI color audit passed: all literal hex/QSS RGB colors use Theme.hh tokens.')

# Validate the actual product tokens, including the requested brand accent.
source = (root / 'native/host/Theme.hh').read_text()
colors = dict(re.findall(r'inline constexpr auto (\w+) = "(#[0-9A-Fa-f]{6})";', source))
for name, alias in re.findall(r'inline constexpr auto (\w+) = (\w+);', source):
    colors[name] = colors[alias]
for path in (root / 'native/host').iterdir():
    if path.suffix not in ('.cc', '.hh'):
        continue
    for token in re.findall(r'@([A-Za-z]+)@', path.read_text()):
        if token not in colors:
            raise SystemExit(f'{path.name}: unknown QSS token {token}')
def luminance(value):
    values = [int(value[i:i+2], 16) / 255 for i in (1, 3, 5)]
    linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in values]
    return sum(v * w for v, w in zip(linear, (.2126, .7152, .0722)))
def contrast(a, b):
    high, low = sorted((luminance(colors[a]), luminance(colors[b])), reverse=True)
    return (high + .05) / (low + .05)
pairs = [(fg, bg, 4.5) for fg in ('Text', 'Heading', 'Warning', 'Error', 'Success')
         for bg in ('Canvas', 'Surface', 'Raised', 'Hover')]
pairs += [('Muted', bg, 4.5) for bg in ('Canvas', 'Surface', 'Raised')]
pairs += [('Heading', 'Selection', 4.5), ('Text', 'Selection', 4.5), ('OnAccent', 'Accent', 4.5)]
pairs += [(fg, bg, 3) for fg in ('Accent', 'Border') for bg in ('Canvas', 'Surface', 'Raised')]
for foreground, background, minimum in pairs:
    ratio = contrast(foreground, background)
    if ratio < minimum:
        raise SystemExit(f'{foreground}/{background}: {ratio:.2f}:1 < {minimum}:1')
print(f'Theme contrast passed: {len(pairs)} text/control pairs (not a full accessibility audit).')
