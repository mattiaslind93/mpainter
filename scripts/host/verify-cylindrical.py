"""Native cylindrical fill, angular crop, backface culling and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'projection.obj';objects=0;lines=[]
for line in (root/'native/tests/fixtures/single_tile.obj').read_text().splitlines():
 if line.startswith('o '):objects+=1
 if line.startswith('mtllib '):line='mtllib projection.mtl'
 if objects>1 and line.startswith('usemtl '):line='usemtl OtherSurface'
 lines.append(line)
mesh.write_text('\n'.join(lines)+'\n');mesh.with_suffix('.mtl').write_text((root/'native/tests/fixtures/single_tile.mtl').read_text()+'\nnewmtl OtherSurface\nKd .5 .5 .5\n')
# Projection offsets use the complete scene bounds; center the bounded decal on
# the first object's native X/-Z/Y coordinates without mutating the mesh.
points=[];selected=[];object_index=0
for line in lines:
 if line.startswith('o '):object_index+=1
 if line.startswith('v '):
  x,y,z=map(float,line.split()[1:4]);point=(x,-z,y);points.append(point)
  if object_index==1:selected.append(point)
low=[min(v[c] for v in points) for c in range(3)];high=[max(v[c] for v in points) for c in range(3)];extent=max(b-a for a,b in zip(low,high))
center=[((min(v[c] for v in selected)+max(v[c] for v in selected))-(low[c]+high[c]))*.5/extent for c in range(3)]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_PLANAR_DEMO='1',BTS_SPATIAL_MODE_DEMO='4')
env.update({f'BTS_PLANAR_CENTER_{i}':str(v) for i,v in enumerate(center)})
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'spatial.json').read_text());assert proof['pid']==process.pid and proof['mode']==4
c={v['stage']:v for v in proof['captures']}
def projection(v):return v['document']['texture_sets'][0]['layers'][-1]['fill_projection']
for v in c.values():
 assert v['decimal']=='.' and not v['shader_failures'] and v['document']['schema']==58
 assert len(v['other_sets'])==1 and v['uv_texels']==c['spatial']['uv_texels']
 for current,original in zip(v['other_sets'],c['spatial']['other_sets']):
  assert current['name']==original['name'] and max(abs(x-y) for x,y in zip(current['native_values'],original['native_values']))<1e-6
  if 'hash' in current and 'hash' in original:assert current['hash']==original['hash']
assert projection(c['spatial'])['mode']==4
for key in ['scale','rotation','offset','offset3','scale3','rotation3']:assert projection(c['triplanar'])[key]==projection(c['spatial'])[key]
assert not c['spatial']['depth_visible'] and c['spatial']['backface_visible'] and c['spatial']['cylinder_angle_visible'] and c['spatial']['offset_visible']
assert projection(c['scale-one'])['scale']==[1,1] and not c['scale-one']['scale_v_enabled']
assert c['triplanar']['native_hash']!=c['spatial']['native_hash']!=c['scale-one']['native_hash']
assert c['undo-scale']['native_hash']==c['spatial']['native_hash']
for before,after in [('undo-scale','translated'),('translated','rotated'),('rotated','scaled')]:assert c[before]['native_hash']!=c[after]['native_hash']
assert 0<c['cropped']['visible_texels']<c['scaled']['visible_texels']
assert c['no-wrap']['visible_texels']<c['cropped']['visible_texels']
assert projection(c['half-cylinder'])['projection_options']['cylinder_angle']==180
assert c['half-cylinder']['native_hash']!=c['no-wrap']['native_hash']
assert c['culled-cylinder']['native_hash']!=c['half-cylinder']['native_hash']
assert c['undo-cull']['native_hash']==c['half-cylinder']['native_hash']
assert c['saved']['document']==c['opened']['document']
assert c['saved']['native_hash']==c['opened']['native_hash']==c['undo-open-edit']['native_hash']
assert c['edited-after-open']['native_hash']!=c['opened']['native_hash']
assert c['reference']['composite_checked'] and not c['reference']['composite_differences'],c['reference']['composite_differences']
assert not (a.output/'spatial-source.png').exists()
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',cylindrical_3d_and_uv=True,angle_and_radial_backface=True,retained_transforms=True,linked_uv_scale=True,planar_depth_hidden=True,cylinder_crop=True,wrap=True,other_texture_set_unchanged=True,undo=True,packed_save_open=True,exact_async_sync_composite=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
