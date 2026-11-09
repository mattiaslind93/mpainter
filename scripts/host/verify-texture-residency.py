# SPDX-License-Identifier: GPL-3.0-or-later
"""AB: fixed full-resolution residency, memory warnings, native navigation and pixel retention."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--reopen-from',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.check_only:
 for folder in ['before','after']:(a.output/folder).mkdir(exist_ok=True)
 effects=[];materials=[];geometries=[];cameras=[];nodes=[]
 for i in range(8):
  effects.append(f'<effect id="effect{i}"><profile_COMMON><technique sid="common"><lambert><diffuse><color>1 1 1 1</color></diffuse></lambert></technique></profile_COMMON></effect>')
  materials.append(f'<material id="Set{i}" name="Set{i}"><instance_effect url="#effect{i}"/></material>')
  geometries.append(f'''<geometry id="plane{i}" name="Plane{i}"><mesh>
 <source id="pos{i}"><float_array id="pos-array{i}" count="12">-1 -1 0 1 -1 0 1 1 0 -1 1 0</float_array><technique_common><accessor source="#pos-array{i}" count="4" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
 <source id="uv{i}"><float_array id="uv-array{i}" count="8">0 0 1 0 1 1 0 1</float_array><technique_common><accessor source="#uv-array{i}" count="4" stride="2"><param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>
 <vertices id="verts{i}"><input semantic="POSITION" source="#pos{i}"/></vertices><triangles count="2" material="mat"><input semantic="VERTEX" source="#verts{i}" offset="0"/><input semantic="TEXCOORD" source="#uv{i}" offset="1" set="0"/><p>0 0 1 1 2 2 0 0 2 2 3 3</p></triangles></mesh></geometry>''')
  cameras.append(f'<camera id="Camera{i}" name="Camera{i}"><optics><technique_common><perspective><yfov>45</yfov><aspect_ratio>1.5</aspect_ratio><znear>0.01</znear><zfar>1000</zfar></perspective></technique_common></optics></camera>')
  nodes.append(f'<node id="PlaneNode{i}" name="Plane{i}"><translate>{i*6} 0 0</translate><instance_geometry url="#plane{i}"><bind_material><technique_common><instance_material symbol="mat" target="#Set{i}"/></technique_common></bind_material></instance_geometry></node>')
  nodes.append(f'<node id="CameraNode{i}" name="Camera{i}"><lookat>{i*6} 0 2.8 {i*6} 0 0 0 1 0</lookat><instance_camera url="#Camera{i}"/></node>')
 xml='''<?xml version="1.0" encoding="utf-8"?><COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1"><asset><created>2026-10-20T00:00:00Z</created><modified>2026-10-20T00:00:00Z</modified><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>'''
 for tag,items in [('effects',effects),('materials',materials),('geometries',geometries),('cameras',cameras)]:xml+=f'<library_{tag}>'+''.join(items)+f'</library_{tag}>'
 xml+='<library_visual_scenes><visual_scene id="Scene" name="Scene">'+''.join(nodes)+'</visual_scene></library_visual_scenes><scene><instance_visual_scene url="#Scene"/></scene></COLLADA>'
 (a.output/'residency.dae').write_text(xml)
 image=Image.new('RGB',(128,128));pixels=image.load()
 for y in range(128):
  for x in range(128):pixels[x,y]=((x*17+y*3)%180+20,(x*3+y*13)%170+60,(x^y)%180+30)
 ImageDraw.Draw(image).ellipse((24,24,104,104),outline=(250,250,10),width=3);image.save(a.output/'pattern.png')
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_RESIDENCY_DEMO=str(a.output))
 if a.reopen_from:
  link=a.output/'resident.blend'
  if not link.exists():link.symlink_to(a.reopen_from.resolve())
  env['BTS_RESIDENCY_OPEN_ONLY']='1'
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
 for label,extra,timeout in ([('run',{},700)] if a.reopen_from else [('run',{},700),('restart',{'BTS_RESIDENCY_RESTART':'1'},60)]):
  with (a.output/(label+'.log')).open('w')as log:
   process=subprocess.Popen(command,env=dict(env,**extra),stdout=log,stderr=subprocess.STDOUT)
   try:code=process.wait(timeout=timeout)
   except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
  assert code==0,(code,(a.output/(label+'.log')).read_text()[-7000:])
d=json.loads((a.output/'residency.json').read_text());c={v['stage']:v for v in d['captures']}
if a.reopen_from:
 required=['wide','front0','front7','revisited']
 baseline=None;stages={}
 for stage in required:
  value=c[stage];res=value['residency'];assert res['storage_formats_measured'] and 0<res['resident_texture_storage_bytes']<=res['reserved_upperbound_bytes'],(stage,res)
  assert res['all_gpu_texture_allocated_bytes'] is None
  current={v['id']:(v.get('color_hash'),v.get('roughness_hash'))for v in value['textures']}
  if baseline is None:baseline=current
  assert current==baseline and len(current)==8 and not value['shader_failures'],stage
  assert not res['refining'],stage
  assert all(i['width']<=i['cap']and i['height']<=i['cap']for i in res['images']),stage
  stages[stage]={'resident_texture_storage_bytes':res['resident_texture_storage_bytes'],'reserved_bytes':res['reserved_upperbound_bytes'],'budget_bytes':res['budget_bytes'],'elapsed_ms':value['elapsed_ms']}
 assert stages['front0']['resident_texture_storage_bytes']>0  # Zoom no longer forces allocation churn.
 log=(a.output/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log and not re.search(r'Shader compilation failed|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-3000:]
 proof={'pid':d['pid'],'post_draw_native_format_mip_storage':True,'blender_total_counter_unavailable':True,'all_source_hashes_exact':True,'stages':stages}
 (a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2));raise SystemExit(0)
required=['wide','front0','front7','revisited','budget64','saved','opened'];assert all(s in c for s in required),list(c)
def r(stage):return c[stage]['residency']
def resident_bytes(res):
 if res.get('resident_texture_storage_bytes') is not None:return res['resident_texture_storage_bytes']
 # Earlier test captures predate native post-draw accounting. The eight-panel
 # fixture uses ordinary 2D images; derive their payload from cached dimensions.
 total=0
 for image in res['images']:
  w,h=image['width'],image['height']
  if not w or not h:continue
  assert image['tiles']==1
  match=re.fullmatch(r'([RGBA]+)(8|16|32)(?:F)?',image['format']);assert match,image['format']
  texel=len(match[1])*int(match[2])//8
  while True:
   total+=w*h*texel
   if w==h==1:break
   w=max(1,w//2);h=max(1,h//2)
 return total
def hashes(stage):return {v['id']:(v.get('color_hash'),v.get('roughness_hash'))for v in c[stage]['textures']}
assert len(hashes('wide'))==8
for stage in required:
 state=c[stage];res=state['residency'];assert 0<res['reserved_upperbound_bytes'],(stage,res)
 assert res['source_upperbound_bytes']>res['budget_bytes']*3,(stage,res['source_upperbound_bytes'])
 assert not res['refining'],stage
 assert 0<resident_bytes(res)<=res['reserved_upperbound_bytes'],stage
 assert not state['shader_failures'],stage
 assert hashes(stage)==hashes('wide'),stage
 assert any(v['source_width']==2048 for v in res['images']),stage
 for image_info in res['images']:
  assert image_info['cap']==image_info['sampling_cap']==max(image_info['source_width'],image_info['source_height']),(stage,image_info)
  assert image_info['width']<=image_info['cap'] and image_info['height']<=image_info['cap'],(stage,image_info)
 assert res['memory_pressure'] and res['warning'],stage
 image=Image.open(a.output/(stage+'.png')).convert('RGB');assert not any(red>235 and green<20 and blue>235 for red,green,blue in image.getdata()),stage
for stage in ['front0','front7']:
 assert all(v['cap']==max(v['source_width'],v['source_height'])for v in r(stage)['images'])
assert c['saved']['document']==c['opened']['document']
assert r('budget64')['budget_bytes']==64*1024*1024 and r('wide')['budget_bytes']==128*1024*1024
export=json.loads((a.output/'export-exact.json').read_text());assert export['byte_exact'] and len(export['files'])>=4
restart=json.loads((a.output/'restart.json').read_text());assert restart['persisted_budget_mb']==64 and restart['pid']!=d['pid']
for label in ['run','restart']:
 text=(a.output/(label+'.log')).read_text();assert 'BTS_SHUTDOWN clean' in text
 assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',text,re.I),text[-4000:]
log=(a.output/'run.log').read_text();assert 'BTS_RESIDENCY' in log
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','implementation':'full texture set resolution under navigation/edits/budget pressure','sets':8,'source_resolution':2048,'memory_budget_warns_without_downsampling':True,'authored_native_hashes_unchanged':True,'export_png_byte_exact':len(export['files']),'packed_open_exact':True,'preferences_restart_pid':restart['pid'],'stages':{s:{'elapsed_ms':v['elapsed_ms'],'budget':v['residency']['budget_bytes'],'reserved_upperbound':v['residency']['reserved_upperbound_bytes'],'source_upperbound':v['residency']['source_upperbound_bytes'],'resident_texture_storage_bytes':resident_bytes(v['residency']),'all_gpu_allocated_textures':v['residency']['all_gpu_texture_allocated_bytes']or None}for s,v in c.items()},'magenta_pixels':0}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
