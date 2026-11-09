# SPDX-License-Identifier: GPL-3.0-or-later
"""Native editable filled contours, independent channels, opacity and .blend roundtrip."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'filled-plane.obj';mesh.write_text('mtllib filled-plane.mtl\no FilledSurface\nusemtl FilledSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n');mesh.with_suffix('.mtl').write_text('newmtl FilledSurface\nKd .4 .4 .4\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_FILLED_PATH_DEMO='1',BTS_RIBBON_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=240)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'filled-path.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
for name,v in c.items():
 assert len(v['paths'])==1 and v['paths'][0]['version']==5 and v['document']['schema']==79
 assert v['decimal']=='.' and not v['shader_failures']
 assert v['paths'][0]['filled']==(name!='brush-mode')
assert not c['two-points']['paths'][0]['closed'] and c['filled']['paths'][0]['closed']
assert c['two-points']['hash']!=c['filled']['hash']
assert c['filled']['hash']==c['undo-opacity']['hash']
assert c['half-opacity']['hash']!=c['filled']['hash']
assert abs(c['filled']['ink_probe'][0][3]-1)<1e-6
assert abs(c['half-opacity']['ink_probe'][0][3]-.5)<1e-6
assert abs(c['filled']['ink_probe'][2][0]-.8)<1e-6
assert abs(c['roughness']['ink_probe'][2][0]-.2)<1e-6
assert c['filled']['channel_hashes'][0]==c['roughness']['channel_hashes'][0]
assert c['roughness-disabled']['channel_hashes'][2]==c['two-points']['channel_hashes'][2]
assert c['undo-channel']['channel_hashes']==c['roughness']['channel_hashes']
assert c['curved']['hash']!=c['undo-channel']['hash']
assert c['colored']['hash']!=c['curved']['hash']
assert c['undo-color']['hash']==c['curved']['hash']
assert c['redo-color']['hash']==c['colored']['hash']
assert c['colored']['undo_count']==c['curved']['undo_count']+1
assert c['saved']['document']==c['opened']['document']
assert c['saved']['native_hash']==c['opened']['native_hash']
assert c['open-contour']['hash']==c['two-points']['hash']
assert c['undo-close']['hash']==c['opened']['hash']==c['undo-mode']['hash']
assert c['brush-mode']['hash']!=c['undo-mode']['hash']
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',closed_editable_surface_contour=True,uniform_channels=True,color_picker_single_undo=True,roughness_and_channel_toggle=True,final_shape_opacity=True,curve_edit=True,open_contour_empty=True,separate_brush_mode=True,undo=True,exact_save_open=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
