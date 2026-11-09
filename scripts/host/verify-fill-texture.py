# SPDX-License-Identifier: GPL-3.0-or-later
"""Native fill textures through file and Shelf, UV transforms, Atom, triplanar and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_FILL_TEXTURE_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'fill-texture.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
for stage in ['file-bound','undo-offset','undo-atom','shelf-bound','triplanar','saved','opened-packed']:
 assert c[stage]['fill_texture']=='fill-pair' and c[stage]['fill_texture_resolved'] and not c[stage]['fill_atom'],(stage,c[stage])
 assert c[stage]['resource_label']=='fill-pair' and c[stage]['thumbnail_colors']>1
assert not c['uniform']['fill_texture'] and not c['uniform-again']['fill_texture']
assert c['atom']['fill_atom'] and not c['atom']['fill_texture']
assert c['shelf-filter']['shelf_filter']=='u:texture'
red=[.6131324,.0701244,.0205877];blue=[.0474167,.0134515,.8697854]
for got,expected in zip(c['file-bound']['texture_probes'],[red,blue]):assert max(abs(x-y) for x,y in zip(got[:3],expected))<.011,(got,expected)
for got,expected in zip(c['offset']['texture_probes'],[blue,red]):assert max(abs(x-y) for x,y in zip(got[:3],expected))<.011,(got,expected)
assert c['file-bound']['composite_hash']==c['undo-offset']['composite_hash']==c['undo-atom']['composite_hash']==c['shelf-bound']['composite_hash']
assert c['triplanar']['fill_mode']==1 and c['triplanar']['composite_hash']!=c['file-bound']['composite_hash']
assert c['reference']['composite_checked'] and not c['reference']['composite_differences'],c['reference']['composite_differences']
assert c['saved']['composite_hash']==c['opened-packed']['composite_hash']
assert c['saved']['fill_texture_hash']==c['opened-packed']['fill_texture_hash']
assert proof['source_file_absent'] and not (a.output/'fill-pair.png').exists()
assert all(v['decimal']=='.' and not v['shader_failures'] for v in c.values())
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_LAYER_PREVIEW_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',file_picker=True,shelf_texture=True,resource_name=True,linear_acescg=True,uv_mapping=True,triplanar=True,thumbnail=True,atom_retained=True,undo=True,exact_native_composite=True,packed_save_open_without_source=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
