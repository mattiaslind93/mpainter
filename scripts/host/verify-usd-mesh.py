# SPDX-License-Identifier: GPL-3.0-or-later
"""Native USDC/USDZ mesh engine probe; textured Painter preset is a separate step."""
from parity_test_environment import private_display
import argparse,json,os,re,struct,subprocess,zipfile
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'UsdMesh.obj';lines=[];index=0
for line in (root/'native/tests/fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib UsdMesh.mtl').splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 lines.append(line)
mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'native/tests/fixtures/single_tile.mtl').read_text();(a.output/'UsdMesh.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_MESH_EXPORT_DEMO='1',BTS_MESH_EXPORT_USD='1',BTS_SKEW_DEMO='1')
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=300)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
 for file in ['triangles.usdc','polygons.usdz']:
  check=dict(env,QT_QPA_PLATFORM='offscreen',BTS_USD_MESH_VERIFY_INPUT=str(a.output/file))
  with (a.output/(file+'.inspect.log')).open('w')as log:code=subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--cpu-export-test',str(a.output/(file+'-check'))],env=check,stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
  assert code==0,(a.output/(file+'.inspect.log')).read_text()[-3000:]
assert (a.output/'triangles.usdc').read_bytes().startswith(b'PXR-USDC')
inspections={name:json.loads((a.output/(name+'-check/usd-inspected.json')).read_text())for name in ['triangles.usdc','polygons.usdz']}
for name,d in inspections.items():
 assert d['points']==1324 and d['triangles']==2640 and d['faces']==(2640 if name.endswith('.usdc')else 1368),d
 assert len(d['meshes'])==2 and len(d['materials'])==2 and d['preview_shaders']==2
 assert set(d['materials'])=={'CubeSurface','SphereSurface'}and all(m['uvs']>0 and m['material']in d['materials']for m in d['meshes'])
 assert d['up_axis']=='Z'and d['meters_per_unit']==1 and d['default_prim']and d['texture_assets']==[]
with zipfile.ZipFile(a.output/'polygons.usdz')as archive:
 members=archive.infolist();assert len(members)==1 and members[0].filename.endswith('.usdc')and archive.read(members[0]).startswith(b'PXR-USDC')
 with (a.output/'polygons.usdz').open('rb')as file:
  for member in members:
   assert member.compress_type==zipfile.ZIP_STORED;file.seek(member.header_offset);header=file.read(30);name_length,extra_length=struct.unpack_from('<HH',header,26);assert (member.header_offset+30+name_length+extra_length)%64==0
proof=json.loads((a.output/'mesh-export.json').read_text());c={s['stage']:s for s in proof['captures']};base=c['before']
for name,s in c.items():
 assert s['document']==base['document']and s['source_obj_hash']==base['source_obj_hash']and s['undo_count']==base['undo_count'],name
 assert s['decimal']=='.'and not s['shader_failures']
 for key in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][key]==base['view'][key],(name,key)
 im1=Image.open(a.output/'before-viewport.png').convert('RGB');im2=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert sum(ImageStat.Stat(ImageChops.difference(im1,im2)).mean)/765<.001,name
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proof['pid'],locale='sv_SE.UTF-8',native_blender_usd_writer=True,usdc_crate=True,usdz_uncompressed_aligned64=True,real_material_bindings=True,uvs_preserved=True,inspections=inspections,scene_layers_camera_undo_unchanged=True,textured_preset_implemented=False,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
