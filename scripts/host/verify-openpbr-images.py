# SPDX-License-Identifier: GPL-3.0-or-later
"""Native File New -> background OpenPBR image-to-fill adoption, GPU texels and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,shutil
from datetime import datetime
from pathlib import Path
from PIL import Image,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--package',action='store_true');p.add_argument('--missing-tile',action='store_true');p.add_argument('--binary',type=Path);p.add_argument('--legacy-project',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))
binary=a.binary.resolve()if a.binary else bts/'build/host/bin/bts-host'
if a.legacy_project:a.missing_tile=True;shutil.copyfile(a.legacy_project,a.output/'skew.blend')
source=a.output/'source';source.mkdir(exist_ok=True)
with (a.output/'source-generation.log').open('w')as log:subprocess.run([str(binary),'--cpu-export-test',str(source)],env=dict(os.environ,QT_QPA_PLATFORM='offscreen'),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
folder=source/'openpbr';mesh=folder/('material-scene.usdz'if a.package else'material-scene.usda');assert mesh.exists(),mesh
if a.missing_tile:(folder/'Set_A_base_color_1002.exr').unlink()
original={str(f.relative_to(folder)):f.read_bytes()for f in folder.rglob('*')if f.is_file()}
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_PERFORMANCE_PROFILE='1',BTS_OPENPBR_IMAGES_DEMO='1',BTS_OPENPBR_IMPORT_DEMO='1',BTS_OPENPBR_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_RESOURCE_CACHE=str(a.output/'resource-cache'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_TEMPLATE_DIR=str(a.output/'templates'))
if a.legacy_project:env['BTS_OPENPBR_DEFERRED_LEGACY']='1'
if a.binary:env['BTS_HOST_EXECUTABLE']=str(binary)
if a.missing_tile:env['BTS_OPENPBR_DEFERRED_DEMO']='1'
try:
 with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
finally:
 held=Path(str(folder)+'.held')
 if held.exists():held.rename(folder)
d=json.loads((a.output/'openpbr-images.json').read_text());c={x['stage']:x for x in d['captures']}
if a.missing_tile:
 assert set(c)=={'deferred','deferred-opened'}and d['visible_error']
 for name,s in c.items():
  assert not s['add_paint_enabled']and not s['shader_failures']
  sets=s['document']['texture_sets'];assert len(sets)==1 and not sets[0]['uniform_editable']and len(sets[0]['layers'])==1
  assert '1002'in s['import_status']and 'missing'in s['import_status'].lower(),s['import_status']
  info=[v['imported_source']for v in s['shader_nodes'].values()if'imported_source'in v];assert len(info)==1 and not info[0].get('converted',False)and len(info[0]['images'])==5
 assert c['deferred']['document']==c['deferred-opened']['document']
 assert original=={str(f.relative_to(folder)):f.read_bytes()for f in folder.rglob('*')if f.is_file()}
 log=(a.output/'run.log').read_text();assert 'BTS_USD_MATERIAL_INSTALLED'not in log and 'BTS_USD_MATERIAL_ERROR'in log and f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
 result={'legacy_schema_migration':bool(a.legacy_project),'missing_tile_visible_error':True,'source_native_material_retained':True,'no_partial_layer_or_undo':c['deferred']['undo_count']==0,'read_only_packed_save_open_exact':True,'original_source_unchanged':True,'pid':d['pid']};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(0)
for name,s in c.items():
 assert s['add_paint_enabled']and not s['shader_failures'],(name,s['import_status'])
 sets=s['document']['texture_sets'];assert len(sets)==1 and sets[0]['tiles']==[1001,1002],sets
 layers=sets[0]['layers'];assert len(layers)==2 and sum(x['name']=='Imported OpenPBR maps'for x in layers)==1
 assert abs(sets[0]['displacement']['midlevel'])<1e-9 and abs(sets[0]['displacement']['strength']-1)<1e-9
 for sample in s['samples']:
  assert sample['valid'];tile=sample['tile'];expected=[.25,.5,.75]if tile==1001 else[.5,.25,.125]
  assert max(abs(x-y)for x,y in zip(sample['color'],expected))<1e-6,sample
  assert abs(sample['metallic']-.25)<1e-6 and abs(sample['roughness']-.5)<1e-6
  assert abs(sample['bump']-(-.125 if tile==1001 else .25))<1e-6,sample
 info=[v['imported_source']for v in s['shader_nodes'].values()if'imported_source'in v];assert len(info)==1 and info[0]['converted']
 distances=[v['bump_distance']for v in s['shader_nodes'].values()if'bump_distance'in v];assert distances and max(abs(x-.03125)for x in distances)<1e-9
 image=Image.open(a.output/(name+'.png')).convert('RGB');assert sum(ImageStat.Stat(image).mean)>10 and max(ImageStat.Stat(image).stddev)>2
assert c['imported']['document']==c['opened-without-sources']['document']
assert original=={str(f.relative_to(folder)):f.read_bytes()for f in folder.rglob('*')if f.is_file()}
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log and 'BTS_USD_MATERIAL_INSTALLED'in log
assert not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK|BTS_FINALIZE_FALLBACK',log,re.I)
pending_log=log.split('BTS_NEW_PROJECT',1)[1].split('BTS_USD_MATERIAL_INSTALLED',1)[0]
assert 'BTS_UV_PREVIEW'not in pending_log,'Temporary native fallback was compiled before material adoption'
startup={key:datetime.fromisoformat(re.search(r'([^\n ]+) \| '+key,log)[1])for key in ['BTS_SCENE_READY','BTS_GPU_READY']}
result={'pid':d['pid'],'source':'USDZ'if a.package else'loose USD','background_image_adoption':True,'temporary_native_fallback_uv_rendered':False,'gpu_material_samples_per_stage':len(c['imported']['samples']),'raw_signed_bump_two_tiles':True,'displacement_midlevel_scale_preserved':True,'native_bump_distance_preserved':.03125,'editable_fill_layer':True,'packed_save_open_without_original_sources_exact_document':True,'sources_unchanged':True,'max_background_ping_ms':d['max_background_ping_ms'],'max_import_ping_ms':d['max_import_ping_ms'],'scene_ready_to_gpu_ready_ms':(startup['BTS_GPU_READY']-startup['BTS_SCENE_READY']).total_seconds()*1000,'scope':'Pinned OpenPBR profiles and five mapped channels; full viewport BSDF remains a compatibility subset.','clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
