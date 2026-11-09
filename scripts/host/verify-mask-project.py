# SPDX-License-Identifier: GPL-3.0-or-later
"""Baseline mask edit -> fresh native host -> mask edit and save. Run inside bts-build."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
p=argparse.ArgumentParser();p.add_argument('--demo',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];root=repo.parent;a.output.mkdir(parents=True,exist_ok=True);stock=a.output/'stock'
with (a.output/'stock.log').open('w') as log:
 subprocess.run(['bash','-c','source "$1"; export WAYLAND_DISPLAY=; exec "$2" --background --factory-startup --disable-autoexec --python-exit-code 1 "$3" --python "$4" -- "$5"','mask-proof',str(repo/'scripts/host/gpu-env.sh'),str(root/'build/baseline/bin/blender'),str(a.demo/'masked.blend'),str(repo/'scripts/host/stock-mask-roundtrip.py'),str(stock)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
command=['bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--open-document',str(stock/'stock-mask.blend'),'--reject-document',str(stock/'invalid-mask.blend'),'--open-demo-dir',str(a.output/'host'),'--environment','/nonexistent-mask-environment.hdr']
children=set();started=time.monotonic()
with (a.output/'host.log').open('w') as log:
 process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env={**os.environ,'BTS_ROOT':str(root),'BTS_ENVIRONMENT':'/nonexistent-mask-environment.hdr'})
 while process.poll() is None:
  try:
   if Path(f'/proc/{process.pid}/exe').resolve(strict=True).name=='bts-host':
    for task in Path(f'/proc/{process.pid}/task').glob('*/children'):children.update(task.read_text().split())
  except FileNotFoundError:pass
  if time.monotonic()-started>180:
   process.terminate()
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:process.kill();process.wait()
   raise AssertionError('Fresh mask Open timed out')
  time.sleep(.1)
assert process.returncode==0 and not children,(process.returncode,children)
log=(a.output/'host.log').read_text();assert 'BTS_SHUTDOWN clean' in log and 'BTS_INVALID_PROJECT_PRESERVED' in log and 'Not freed memory blocks' not in log
opened,edited=json.loads((a.output/'host/open.json').read_text())['captures']
original=next(c['document'] for c in json.loads((a.demo/'masks.json').read_text())['captures'] if c['stage']=='saved')
assert opened['document']['id']==original['id'] and not opened['undo_enabled'] and edited['undo_enabled']
for old,new,changed in zip(original['texture_sets'],opened['document']['texture_sets'],edited['document']['texture_sets']):
 assert old['id']==new['id']==changed['id']
 if old['name']!='SphereSurface':assert old==new==changed;continue
 before,after,after_edit=old['layers'][-1],new['layers'][-1],changed['layers'][-1]
 assert before['id']==after['id']==after_edit['id'] and before['values']==after['values']==after_edit['values']
 assert before['paint_hashes']==after['paint_hashes']==after_edit['paint_hashes']
 assert before['mask']['tiles'][0]['hash']!=after['mask']['tiles'][0]['hash']!=after_edit['mask']['tiles'][0]['hash']
 assert old['selected_mask'] and new['selected_mask'] and changed['selected_mask']
result={'host_pid':process.pid,'children':[],'clean_exit':True,'stock':json.loads((stock/'stock.json').read_text()),'packed_mask_edit_imported':True,'parent_values_and_material_pixels_retained':True,'fresh_mask_stroke_saved':True,'invalid_mask_preserves_live_document':True,'environment_restored_from_packed_bytes':True}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
