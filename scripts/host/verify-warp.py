"""Native Warp tensor-grid editing, exact split, async reference and packed project."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--gpu',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'warp-plane.obj'
mesh.write_text('mtllib warp-plane.mtl\no WarpPlane\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nusemtl WarpSurface\nf 1/1/1 2/2/1 3/3/1 4/4/1\n')
mesh.with_suffix('.mtl').write_text('newmtl WarpSurface\nKd 1 1 1\nPr .4\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_PLANAR_DEMO='1',BTS_WARP_DEMO='1')
if a.gpu:env.update(BTS_VERIFY_GPU_STACK='1',BTS_GPU_DENSE_ORACLE='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'warp.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
def projection(v):return v['document']['texture_sets'][0]['layers'][-1]['fill_projection']
def grid(v):return projection(v)['warp']['grid']
for v in c.values():
 assert v['decimal']=='.' and not v['shader_failures'] and v['document']['schema']>=58
 assert v['uv_texels']==c['flat']['uv_texels']
assert projection(c['flat'])['mode']==5 and c['flat']['columns_enabled']
assert projection(c['vertices'])['warp']['edit']
assert c['deformed']['native_hash']!=c['flat']['native_hash'] and not c['deformed']['columns_enabled']
assert c['deformed']['undo_count']==c['vertices']['undo_count']+1
assert grid(c['deformed'])!=grid(c['flat'])
assert c['undo-deform']['native_hash']==c['flat']['native_hash']
assert c['redo-deform']['native_hash']==c['deformed']['native_hash']==c['cancelled']['native_hash']
assert c['cancelled']['undo_count']==c['redo-deform']['undo_count']
assert c['multi']['warp_selection']==[3,4,5],c['multi']['warp_selection']
assert c['removed']['warp_selection']==[4,5]
assert not grid(c['tangents'])['automatic_tangents']
assert len(grid(c['split'])['columns'])==4 and len(grid(c['split'])['rows'])==4
assert c['split']['selected_point']==10
# Independent Hermite evaluator checks exact surface preservation after grid split.
def evaluate(g,u,v):
 def interval(knots,t):return max(0,min(len(knots)-2,next((i-1 for i,k in enumerate(knots) if k>t),len(knots)-2)))
 def basis(t,span):return ([2*t**3-3*t*t+1,-2*t**3+3*t*t],[(t**3-2*t*t+t)*span,(t**3-t*t)*span])
 i=interval(g['columns'],u);j=interval(g['rows'],v);su=g['columns'][i+1]-g['columns'][i];sv=g['rows'][j+1]-g['rows'][j];a=basis((u-g['columns'][i])/su,su);b=basis((v-g['rows'][j])/sv,sv);out=[0.,0.,0.]
 for y in range(2):
  for x in range(2):
   node=g['nodes'][(j+y)*len(g['columns'])+i+x]
   for dv in range(2):
    for du in range(2):
     value=node[du+2*dv]
     for k in range(3):out[k]+=value[k]*a[du][x]*b[dv][y]
 return out
for j in range(15):
 for i in range(15):assert max(abs(x-y) for x,y in zip(evaluate(grid(c['tangents']),i/14,j/14),evaluate(grid(c['split']),i/14,j/14)))<1e-9
assert c['split-cancelled']['native_hash']==c['split']['native_hash']
assert grid(c['reset-selected'])!=grid(c['split']) and grid(c['undo-reset'])==grid(c['split'])
assert c['short-depth']['visible_texels']==0
for before,after in [('undo-reset','global-offset'),('global-offset','global-rotation'),('global-rotation','global-scale')]:assert c[before]['native_hash']!=c[after]['native_hash'],(before,after)
assert c['cropped']['visible_texels']<c['global-scale']['visible_texels']
assert c['no-wrap']['visible_texels']<=c['cropped']['visible_texels']
assert c['reference']['composite_checked'] and not c['reference']['composite_differences'],c['reference']['composite_differences']
assert c['saved']['document']==c['opened']['document'] and c['saved']['native_hash']==c['opened']['native_hash']==c['undo-open']['native_hash']
assert grid(c['edited-open'])!=grid(c['opened'])
assert c['reset-all']['columns_enabled'] and len(grid(c['resized'])['rows'])==5
for key,value in [('offset3',[0,0,0]),('rotation3',[0,0,0]),('scale3',[1,1,1])]:assert projection(c['reset-global'])[key]==value
assert not projection(c['transform-mode'])['warp']['edit']
assert projection(c['gizmo-moved'])['offset3']!=projection(c['transform-mode'])['offset3']
assert projection(c['undo-gizmo'])['offset3']==projection(c['transform-mode'])['offset3']
assert projection(c['redo-gizmo'])['offset3']==projection(c['gizmo-moved'])['offset3']
assert grid(c['rotated-tangents'])!=grid(c['rotate-tangent-tool'])
assert grid(c['scaled-tangents'])!=grid(c['rotated-tangents'])
assert grid(c['surface-drag'])['nodes'][10][0]!=grid(c['scaled-tangents'])['nodes'][10][0]
assert abs(grid(c['surface-drag'])['nodes'][10][0][1])<1e-6
assert projection(c['mirrored'])['scale3'][0]<0
assert projection(c['undo-mirror'])['scale3']==projection(c['surface-drag'])['scale3']
assert len(grid(c['split-click'])['columns'])==len(grid(c['split-click-before'])['columns'])+1
assert len(grid(c['split-click'])['rows'])==len(grid(c['split-click-before'])['rows'])
assert c['split-click']['undo_count']==c['split-click-before']['undo_count']+1
assert projection(c['uv-filter'])['projection_options']['filtering']==2
assert projection(c['uv-filter'])['scale']==[2,2] and projection(c['uv-filter'])['rotation']==15
assert projection(c['physical'])['physical'] and not projection(c['tiling-restored'])['physical']
assert c['physical']['native_hash']!=c['uv-filter']['native_hash']==c['tiling-restored']['native_hash']
assert not c['final']['composite_differences'],c['final']['composite_differences']
for v in c.values():assert not v['property_pending']
for stage in ['vertices','deformed','split','global-scale','reference']:
 for key in ['scene_center','scene_yaw','scene_pitch','scene_distance']:assert c[stage]['view'][key]==c['flat']['view'][key]
assert not (a.output/'warp-source.png').exists()
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',warp_grid=True,viewport_ctrl_drag=True,shift_v=True,multi_selection=True,manual_tangents=True,shape_preserving_split=True,depth=True,crop_wrap=True,global_transforms=True,undo_redo_cancel=True,packed_save_open=True,exact_async_sync_composite=True)
if a.gpu:
 probes=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log)
 assert probes and all(p[2]=='1' for p in probes)
 assert 'BTS_GPU_WARP_VERIFIED' in log and 'BTS_GPU_STACK_FALLBACK' not in log and 'BTS_GPU_CPU_FALLBACK' not in log
 result['gpu']={'probes':len(probes),'maximum_error':max(float(p[1]) for p in probes),'fallbacks':0}
 native=re.findall(r'BTS_NATIVE_GPU_ORACLE channels=(\d+) probes=(\d+) tiles=(\d+) max_error=(\S+) full_resolution=1',log)
 assert len(native)==len(c) and all(int(v[1])>=128*128 for v in native),(len(native),len(c))
 result['gpu']['actual_native_images']={'captures':len(native),'texels_checked':sum(int(v[1])for v in native),'maximum_error':max(float(v[3])for v in native),'full_resolution':True}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
