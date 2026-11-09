# SPDX-License-Identifier: GPL-3.0-or-later
"""Native Alembic New, painting, independent .blend Open and layer-preserving reimport."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2];bts=root.parent
baseline=bts/'build/baseline/bin/blender';fixtures=a.output/'fixtures'
with (a.output/'fixtures.log').open('w') as log:subprocess.run([str(baseline),'--background','--factory-startup','--python',str(root/'scripts/host/generate-alembic-fixture.py'),'--',str(fixtures)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_ALEMBIC_DEMO='1',BTS_DISPLACEMENT_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(fixtures/'surface.abc'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=240)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'alembic.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
for v in c.values():
 assert v['has_document'] and v['texture_sets'] and v['document']['source_mesh'].lower().endswith('.abc')
 assert v['decimal']=='.' and not v['shader_failures']
 assert v['picked_uv'] and len(v['geometry']['objects'])==1
 assert v['geometry']['objects'][0]['source_vertices']==114,v['geometry']
assert c['empty']['hash']!=c['painted']['hash']
for before,after in [('saved','opened-without-source'),('reimport-saved','reimport-opened')]:
 assert c[before]['source_mesh_hash']==c[after]['source_mesh_hash']
 assert c[before]['document']==c[after]['document']
assert c['painted']['layers']==c['reimported']['layers']==2
assert c['empty']['hash']!=c['reimported']['hash']  # Recorded 3D strokes reproject onto the resized surface.
old=c['painted']['document']['texture_sets'][0];new=c['reimported']['document']['texture_sets'][0]
assert old['id']==new['id'] and [l['id'] for l in old['layers']]==[l['id'] for l in new['layers']]
assert c['painted']['source_mesh_hash']!=c['reimported']['source_mesh_hash']
assert c['reimport-opened']['document']==c['invalid-preserved']['document']
assert c['reimport-opened']['source_mesh_hash']==c['invalid-preserved']['source_mesh_hash']
assert not (fixtures/'surface.abc').exists()
inspect=a.output/'inspect.py';inspect.write_text('''import bpy,json,sys
from pathlib import Path
mesh=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(mesh)==1
obj=mesh[0];assert len(obj.data.vertices)==114 and len(obj.data.uv_layers)>0
assert not any(m.type=='MESH_SEQUENCE_CACHE' for o in bpy.data.objects for m in o.modifiers)
assert not any(c.type=='TRANSFORM_CACHE' for o in bpy.data.objects for c in o.constraints)
assert not bpy.data.cache_files
assert abs(obj.location.x-.25)<1e-6
print('BTS_ABC_STOCK_OPEN_OK',len(obj.data.vertices),len(obj.data.uv_layers))
''')
with (a.output/'stock-open.log').open('w') as log:subprocess.run([str(baseline),'--background',str(a.output/'displacement.blend'),'--python',str(inspect)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
assert 'BTS_ABC_STOCK_OPEN_OK' in (a.output/'stock-open.log').read_text()
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',native_alembic=True,lower_and_upper_extension=True,new_file_filter=True,mesh_vertices=114,uvs=True,paint=True,save_open_without_archive=True,reimport_preserves_layer_ids_and_reprojects_paint=True,first_frame_frozen=True,invalid_archive_preserves_project=True,stock_blender_open=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
