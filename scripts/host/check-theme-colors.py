#!/usr/bin/env python3
"""Reject new literal UI hex colors outside the shared theme.

Diagnostic pixel fixtures, material/channel data, and branding vector assets are
not interface colors. QColor/fromRgb calls with computed data remain valid.
"""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
failures = []
sources = [*(root / 'native/host').rglob('*'), *(root / 'native/harness').rglob('*')]
for path in sources:
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
# Theme.hh also carries non-color QSS tokens, such as the shared control radius.
metrics = dict(re.findall(r'inline constexpr auto (\w+) = "([^"#][^"]*)";', source))
for path in sources:
    if path.suffix not in ('.cc', '.hh'):
        continue
    for token in re.findall(r'@([A-Za-z]+)@', path.read_text()):
        if token not in colors and token not in metrics:
            raise SystemExit(f'{path.name}: unknown QSS token {token}')
# Exact source colors take precedence over the previous theme's contrast targets.
import xml.etree.ElementTree as ET
xml = ET.parse(root / 'assets/theme/Turquoise_Dark.xml')
ui = xml.find('.//ThemeUserInterface')
source_roles = {
    'Canvas': (xml.find('.//ThemeOutliner/space/ThemeSpaceGeneric'), 'back'),
    'Surface': (xml.find('.//ThemeProperties/space/ThemeSpaceGeneric'), 'back'),
    'Panel': (ui, 'panel_back'),
    'Viewport': (xml.find('.//ThemeGradientColors'), 'high_gradient'),
    'ViewportEdge': (xml.find('.//ThemeGradientColors'), 'gradient'),
    'Tool': (ui.find('./wcol_tool/ThemeWidgetColors'), 'inner'),
    'MenuButton': (ui.find('./wcol_menu/ThemeWidgetColors'), 'inner'),
    'Header': (xml.find('.//ThemeProperties/space/ThemeSpaceGeneric'), 'header'),
    'Menu': (ui.find('./wcol_menu_back/ThemeWidgetColors'), 'inner'),
    'ToolbarItem': (ui.find('./wcol_toolbar_item/ThemeWidgetColors'), 'inner'),
    'Raised': (ui.find('./wcol_regular/ThemeWidgetColors'), 'inner'),
    'Border': (ui.find('./wcol_regular/ThemeWidgetColors'), 'outline'),
    'Input': (ui.find('./wcol_text/ThemeWidgetColors'), 'inner'),
    'Number': (ui.find('./wcol_num/ThemeWidgetColors'), 'inner'),
    'Accent': (ui.find('./wcol_tool/ThemeWidgetColors'), 'inner_sel'),
    'Text': (ui, 'panel_text'),
    'ListAlternate': (ui.find('./wcol_list_item/ThemeWidgetColors'), 'inner'),
    'Curve': (ui.find('./wcol_curve/ThemeWidgetColors'), 'inner'),
    'ListSelection': (xml.find('.//ThemeOutliner'), 'selected_highlight'),
    'ScrollHandle': (ui.find('./wcol_scroll/ThemeWidgetColors'), 'item'),
    'ScrollHover': (ui.find('./wcol_scroll/ThemeWidgetColors'), 'inner_sel'),
    'Tab': (ui.find('./wcol_tab/ThemeWidgetColors'), 'inner'),
    'TabText': (ui.find('./wcol_tab/ThemeWidgetColors'), 'text'),
    'AxisX': (ui, 'axis_x'), 'AxisY': (ui, 'axis_y'), 'AxisZ': (ui, 'axis_z'),
}
for token, (node, attribute) in source_roles.items():
    assert colors[token].lower() == node.get(attribute)[:7].lower(), token
print(f'Turquoise Dark source mapping passed: {len(source_roles)} exact role colors.')
