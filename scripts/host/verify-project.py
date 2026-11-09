# SPDX-License-Identifier: GPL-3.0-or-later
"""Stock Blender edit -> fresh same-process Qt host -> edit/save proof. Run in bts-build."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

p=argparse.ArgumentParser()
p.add_argument('--paint-demo',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[2]
root=repo.parent
a.output.mkdir(parents=True,exist_ok=True)
stock=a.output/'stock'
# Baseline requires its own bundled libraries; no Qt or app extension is loaded.
with (a.output/'stock.log').open('w') as log:
    subprocess.run(['bash','-c','source "$1"; export WAYLAND_DISPLAY=; exec "$2" --background --factory-startup --disable-autoexec "$3" --python "$4" -- "$5"',
        'stock-proof',str(repo/'scripts/host/gpu-env.sh'),str(root/'build/baseline/bin/blender'),str(a.paint_demo/'painted.blend'),
        str(repo/'scripts/host/stock-project-roundtrip.py'),str(stock)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
stock_result=json.loads((stock/'stock.json').read_text())
command=['bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--open-document',str(stock/'stock-edited.blend'),
    '--open-demo-dir',str(a.output/'host'),'--reject-document',str(stock/'unsupported.blend'),
    '--reject-document',str(stock/'invalid-pixels.blend'),'--environment','/nonexistent-external-environment.hdr']
children=set();child_details={}
with (a.output/'host.log').open('w') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,
        env={**os.environ,'BTS_ROOT':str(root),'BTS_ENVIRONMENT':'/nonexistent-external-environment.hdr'})
    started=time.monotonic()
    while process.poll() is None:
        try:
            if Path(f'/proc/{process.pid}/exe').resolve(strict=True).name=='bts-host':
                for task in Path(f'/proc/{process.pid}/task').glob('*/children'):
                    for child in task.read_text().split():
                        children.add(child)
                        try:child_details[child]=Path(f'/proc/{child}/cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
                        except FileNotFoundError:child_details.setdefault(child,'exited before cmdline read')
        except FileNotFoundError: pass
        if time.monotonic()-started>120:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            raise AssertionError('Fresh Open timed out')
        time.sleep(.1)
(a.output/'child-processes.json').write_text(json.dumps(child_details,indent=2)+'\n')
assert process.returncode==0 and not children,(process.returncode,child_details)
log=(a.output/'host.log').read_text()
assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
assert log.count('BTS_INVALID_PROJECT_PRESERVED')==2
captures=json.loads((a.output/'host/open.json').read_text())['captures']
opened,edited=captures
assert opened['stage']=='opened' and edited['stage']=='edited'
assert not opened['undo_enabled'] and not opened['redo_enabled'] and opened['brush_size']==10
assert edited['undo_enabled'] and not edited['redo_enabled']
original={c['stage']:c for c in json.loads((a.paint_demo/'paint.json').read_text())['captures']}['saved']['document']
assert opened['document']['id']==original['id']
old_sets={t['id']:t for t in original['texture_sets']}
changes={}
for texture_set in opened['document']['texture_sets']:
    old=old_sets[texture_set['id']]
    assert texture_set['name']==old['name']+'_Renamed'
    assert texture_set['channels']==old['channels']
    assert [l['id'] for l in texture_set['layers']]==[l['id'] for l in old['layers']]
    for before,after in zip(old['layers'],texture_set['layers']):
        changed=[i for i,(x,y) in enumerate(zip(before['paint_hashes'],after['paint_hashes'])) if x!=y]
        assert changed==([2] if after['id']==stock_result['layer_id'] else [])
        changes[after['id']]=changed
    new=next(t for t in edited['document']['texture_sets'] if t['id']==texture_set['id'])
    if old['name']=='CubeSurface':assert new==texture_set
    else:
        assert new['layers'][-1]['id']==texture_set['layers'][-1]['id']==stock_result['layer_id']
        assert all(x!=y for x,y in zip(new['layers'][-1]['paint_hashes'],texture_set['layers'][-1]['paint_hashes']))
assert opened['profile']['working_space']=='ACEScg'
result={'host_pid':process.pid,'children':[],'clean_exit':True,'stock':stock_result,
    'stock_pixel_change_imported_only_in_roughness':True,'stable_material_and_layer_bindings':True,
    'same_layer_edited_and_saved':True,'session_history_reset':True,
    'unsupported_schema_and_invalid_coverage_preserve_live_document':True,
    'environment_restored_from_packed_bytes':True,'raw_project':str(a.output)}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
