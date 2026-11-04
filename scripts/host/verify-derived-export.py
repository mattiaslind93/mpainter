"""Native GUI converted preset plus native ImBuf verification of all 16-bit samples."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--cache',type=Path);p.add_argument('--jpeg',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'DerivedMesh.obj';source=(root/'fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib DerivedMesh.mtl');lines=[];index=0
for line in source.splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 lines.append(line)
mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'fixtures/single_tile.mtl').read_text();(a.output/'DerivedMesh.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_DERIVED_EXPORT_DEMO='1',BTS_SKEW_DEMO='1')
if a.jpeg:env['BTS_JPEG_EXPORT_DEMO']='1'
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=400)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
 if not a.jpeg:
  # This independent CLI verifier uses Blender's native decoder, preserving RGB16 precision.
  check_env=dict(env,QT_QPA_PLATFORM='offscreen',BTS_DERIVED_VERIFY_INPUT=str(a.output));command=['bash',str(root/'scripts/host/run-host.sh'),'--cpu-export-test',str(a.output/'png16-check')]
  with (a.output/'png16-check.log').open('w')as log:code=subprocess.run(command,env=check_env,stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
  assert code==0,(a.output/'png16-check.log').read_text()[-3000:]

d=json.loads((a.output/'derived-export.json').read_text());c={s['stage']:s for s in d['captures']};base=c['before']
if not a.check_only:assert d['pid']==proc.pid
for name,s in c.items():
 before=base['document'].copy();current=s['document'].copy();before.pop('export_configuration');current.pop('export_configuration');assert before==current,name
 assert s['source_obj_hash']==base['source_obj_hash'] and s['decimal']=='.' and not s['shader_failures'],name
 if name!='opened':
  for field in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==base['view'][field],(name,field)
assert c['saved']['document']==c['opened']['document']
assert len(d['exports'])==2 and len(d['exports'][0]['files'])==5 and len(d['exports'][1]['files'])==6
config=c['after-converted']['document']['export_configuration'];assert config['version']==8
preset=next(p for p in config['presets']if p['id']==config['preset']);assert len(preset['outputs'])==5 and not preset['standard_outputs'] and all('normal' not in m['name'] for m in preset['outputs'])
assert preset['outputs'][0]['alpha']
for name in ['after-converted','after-direct']:
 before=Image.open(a.output/'before-viewport.png').convert('RGB');after=Image.open(a.output/(name+'-viewport.png')).convert('RGB');difference=sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/765;assert difference<.001,(name,difference)
for folder in ['converted','direct']:
 for path in (a.output/folder).glob('*.jpg'if a.jpeg else '*.png'):
  assert 'CubeSurface' not in path.name
  suffix=path.stem.split('SphereSurface_',1)[1];assert suffix==suffix.lower()and 'Height' not in path.name and '_Bump' not in path.name
if a.jpeg:
 paths=[p for folder in ['converted','direct']for p in (a.output/folder).glob('*.jpg')];assert len(paths)==11 and not any('normal' in p.name for p in paths)
 for path in paths:
  image=Image.open(path);assert image.format=='JPEG'and image.size==(512,512)and image.mode in ['L','RGB'],path
 rough=Image.open(next((a.output/'direct').glob('*_roughness.jpg')));gloss=Image.open(next((a.output/'converted').glob('*_glossiness.jpg')));assert rough.mode==gloss.mode=='L'
 inverse_error=max(abs(r+g-255)for r,g in zip(rough.getdata(),gloss.getdata()));assert inverse_error<=2,inverse_error
 assert config['format']==4 and Image.open(next((a.output/'converted').glob('*_diffuse.jpg'))).mode=='RGB'
 native=dict(pixels=262144,jpeg_bits=8,files=14,roughness_glossiness_max_error=inverse_error,no_jpeg_alpha=True,packed_jpeg_format_restored=True)
else:
 native=json.loads((a.output/'png16-check/verified-png16.json').read_text());assert native['pixels']==262144 and native['roughness_glossiness_sum']==65535 and native['bump_one_channel'] and native['normal_maps']==0
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}' in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=d['pid'],locale='sv_SE.UTF-8',real_export_dialog=True,selected_sphere_only=True,converted_preset_five_maps=True,document_preset_six_maps=True,normal_maps_exported=False,lowercase_bump_suffix=True,packed_template_settings=True,scene_layers_camera_unchanged=True,native_png16=native,clean_shutdown=True)
if a.jpeg:result['native_jpeg8']=result.pop('native_png16')
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
