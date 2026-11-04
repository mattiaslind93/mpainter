"""Exercise real Qt row clicks across three sets, all inspectors and native state."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
for filename in ('verified.json', 'selection.json', 'selection.png', 'shader.png'):
    (output / filename).unlink(missing_ok=True)
root = Path(__file__).resolve().parents[2]
mesh = output / 'three-sets.obj'
(output / 'three-sets.mtl').write_text(''.join(
    f'newmtl Selection{row}\nKd {0.2 * (row + 1)} 0.3 0.4\nPr 0.5\n'
    for row in range(3)))
obj = ['mtllib three-sets.mtl', 'vt 0 0', 'vt 1 0', 'vt 1 1', 'vt 0 1']
for row in range(3):
    x = row * 1.2
    first = row * 4 + 1
    obj += [f'o Panel{row}', f'usemtl Selection{row}',
            f'v {x} 0 0', f'v {x + 1} 0 0', f'v {x + 1} 0 1', f'v {x} 0 1',
            f'f {first}/1 {first + 1}/2 {first + 2}/3 {first + 3}/4']
mesh.write_text('\n'.join(obj) + '\n')
env = dict(os.environ, BTS_RESOURCE_CACHE=str(output / 'resource-cache'), QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
           BTS_TEXTURE_SET_SELECTION_DEMO='1', XDG_CONFIG_HOME=str(output / 'config'),
           XDG_DATA_HOME=str(output / 'data'), XDG_CACHE_HOME=str(output / 'cache'),
           XDG_STATE_HOME=str(output / 'state'), BTS_SESSION_LOG_DIR=str(output / 'logs'),
           BTS_RECOVERY_DIR=str(output / 'recovery'), BTS_TEMPLATE_DIR=str(output / 'templates'))
command = ['bash', str(root / 'scripts/host/run-host.sh'), '--workspace', '--no-splash',
           '--gui-fix-demo', str(output), '--gui-fix-mesh', str(mesh),
           '--gui-fix-resolution', '128', '--autosave-seconds', '3600',
           '--library-dir', str(output / 'library')]
with (output / 'run.log').open('w') as log:
    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=240)
captures = json.loads((output / 'selection.json').read_text())['captures']
assert [entry['row'] for entry in captures] == [2, 0, 1, 2, 1, 0]
for entry in captures:
    row = entry['row']
    assert entry['selected_index'] == row
    assert entry['native_selected_uid'] == entry['view']['selected_material_session_id']
    assert entry['layers'] == row + 1
    assert int(entry['resolution']) == 128 << row
    assert abs(entry['roughness'] - .2 * (row + 1)) < .001
    assert entry['shader'] == f'Selection shader {row}'
(output / 'verified.json').write_text(json.dumps({
    'headless_qt': True, 'texture_sets': 3, 'switches': 6,
    'checks': ['list', 'layers', 'texture_set_settings', 'properties_material',
               'shader_settings', 'viewport_paint_target', 'native_active_material'],
    'captures': captures}, indent=2) + '\n')
print('Texture Set selection: 3 sets, 6 clicks, all inspectors and native viewport PASS')
