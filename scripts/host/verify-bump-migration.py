"""Actual schema45 native image/metadata migration, edit, save and fresh reopen."""
from parity_test_environment import private_display
import argparse
import copy
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
fixture=root/'fixtures/legacy/schema45-painted.blend'
expected=json.loads((fixture.parent/'schema45-painted-document.json').read_text())
assert expected['schema']==45
def migrate(value):
    if isinstance(value,list):return [migrate(item) for item in value]
    if not isinstance(value,dict):return value
    result={key:migrate(item) for key,item in value.items()}
    if 'color' in result and 'normal' in result and 'height' in result:result['bump']=result.pop('height')
    if 'height_method' in result:result['bump_method']=result.pop('height_method')
    return result
expected=migrate(expected);expected['schema']=47
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000')
def run(project,directory):
    directory.mkdir(exist_ok=True)
    with (directory/'run.log').open('w') as log:
        process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--open-document',str(project),'--open-demo-dir',str(directory),'--environment','/nonexistent-external-environment.hdr','--recovery-dir',str(directory/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=180)
        except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
    assert code==0,code
    data=json.loads((directory/'open.json').read_text());assert data['pid']==process.pid
    log=(directory/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
    assert not re.search(r'Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_OPEN_FAILED',log,re.I),log[-4000:]
    return data
first=run(fixture,a.output/'migrated')
opened,edited=first['captures']
legacy_view=copy.deepcopy(opened['document'])
for texture in legacy_view['texture_sets']:
    displacement=texture.pop('displacement');assert not displacement['channel'] and not displacement['allocated']
    for layer in texture['layers']:
        displacement=layer.pop('displacement');assert not displacement['enabled'] and displacement['tiles']==[] and displacement['strokes'] is None
assert legacy_view==expected
assert not opened['undo_enabled'] and edited['undo_enabled']
second=run(a.output/'migrated/edited.blend',a.output/'reopened')
assert second['captures'][0]['document']==edited['document']
counts=[]
for texture in expected['texture_sets']:
    for layer in texture['layers']:
        for tile in layer['paint_tiles']:
            if tile['pages'][4]:counts.append(dict(texture_set=texture['name'],layer=layer['name'],tile=tile['number'],pages=tile['pages'][4],bump_hash=tile['hashes'][4]))
assert counts
result=dict(pids=[first['pid'],second['pid']],source_schema=45,target_schema=47,legacy_values_masks_flags_and_pixels_exact=True,edit_save_fresh_open_exact=True,legacy_scalar_slot4=counts,locale='sv_SE.UTF-8',clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
