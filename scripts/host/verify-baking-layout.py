# SPDX-License-Identifier: GPL-3.0-or-later
"""Offscreen 1024/1920 painting vs baking layout captures."""
from parity_test_environment import private_display
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--before',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_BAKING_LAYOUT_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
if a.before:env['BTS_BAKING_LAYOUT_BEFORE']='1'
with (a.output/'run.log').open('w')as log:
 subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
d=json.loads((a.output/'baking-layout.json').read_text());c={x['stage']:x for x in d['captures']}
if not a.before:
 for width in [1024,1920]:
  paint=c[f'painting-{width}'];bake=c[f'baking-{width}']
  assert bake['window_width']==width and bake['baking_visible'] and bake['baking_separate_window']
  assert bake['viewport_width']==paint['viewport_width'] and bake['viewport_height']==paint['viewport_height']
  assert bake['baking_width']<=width and bake['baking_height']<=bake['window_height']
  assert bake['undo_shortcut_available'] and bake['redo_shortcut_available']
 assert not c['returned']['baking_visible']
 assert not c['none-selected']['bake_enabled'] and 'at least one'in c['none-selected']['bake_tooltip']
 assert c['selected-maps']['bake_enabled'] and c['baked-selected']['mesh_map_roles']==[4,5]
 assert not c['returned-after-bake']['baking_visible'] and not c['closed-to-painting']['baking_visible']
result={'before':a.before,'captures':d['captures']};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
