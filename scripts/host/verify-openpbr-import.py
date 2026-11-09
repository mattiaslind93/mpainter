# SPDX-License-Identifier: GPL-3.0-or-later
"""File New: portable constant OpenPBR import, source retention, template precedence."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))
fixtures=a.output/'fixtures';fixtures.mkdir(exist_ok=True)
subprocess.run([str(bts/'build/host/bin/bts-openpbr-usd-tests'),'--write-import-fixtures',str(fixtures)],check=True,timeout=30)
before={str(f.relative_to(fixtures)):f.read_bytes()for f in fixtures.iterdir()if f.is_file()}
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_OPENPBR_IMPORT_DEMO='1',BTS_OPENPBR_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(fixtures/'supported.usda'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'openpbr-import.json').read_text());c={x['stage']:x for x in d['captures']}
def source(s):
 values=[x['imported_source']for x in s['shader_nodes'].values()if 'imported_source'in x];assert len(values)==1,values
 return values[0]
def assigned(s):
 doc=s['document'];sets=doc['texture_sets'];assert len(sets)==1
 library=doc['shader_instances'];binding=library['texture_sets'][sets[0]['id']]
 return next(x for x in library['instances']if x['id']==binding)['parameters']
for name in ['supported','opened']:
 s=c[name];assert s['shader_workflow']==1 and s['add_paint_enabled']and not s['shader_failures']
 assert assigned(s)['openpbr']['values']['specular_ior']==source(s)['parameters']['values']['specular_ior']
 assert abs(assigned(s)['openpbr']['values']['coat_weight']-.35)<1e-6
 base=s['document']['texture_sets'][0]['base_values'];assert max(abs(x-y)for x,y in zip(base['color'][:3],[.125,.25,.5]))<1e-5,base
 assert abs(base['metallic']-.7)<1e-6 and abs(base['roughness']-.4)<1e-6
 assert source(s)['activate']and source(s)['images']=={}
assert c['supported']['document']==c['opened']['document']
u=c['unsupported'];assert u['shader_workflow']==0 and not source(u)['activate']and abs(source(u)['parameters']['values']['transmission_dispersion_scale']-.6)<1e-6
assert 'transmission_dispersion_scale'in source(u)['reason']and not assigned(u).get('openpbr')
assert source(u)==source(c['unsupported-opened'])and c['unsupported']['document']==c['unsupported-opened']['document']
t=c['explicit-pbr-template'];assert t['shader_workflow']==0 and not assigned(t).get('openpbr')and source(t)['activate']
assert before=={str(f.relative_to(fixtures)):f.read_bytes()for f in fixtures.iterdir()if f.is_file()}
for name in c:
 image=Image.open(a.output/(name+'.png')).convert('RGB');assert sum(ImageStat.Stat(image).mean)>10 and max(ImageStat.Stat(image).stddev)>2
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log and 'BTS_USD_OPENPBR_IMPORT'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'pid':d['pid'],'file_new_dialog':True,'no_template_preserves_supported_constant_profile':True,'base_values_and_portable_shader_exact':True,'editable_material':True,'save_open_exact_document':True,'unsupported_lobe_retained_and_reported':True,'unsupported_metadata_save_open_exact':True,'explicit_pbr_template_wins':True,'source_files_unchanged':True,'real_framebuffers_nonblank':True,'image_import':'Deferred explicitly; native USD material and source profile metadata retained. No automatic image-to-fill adoption yet.','scope':'Portable constant subset only; not full OpenPBR BSDF or modern no-template default.','clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
