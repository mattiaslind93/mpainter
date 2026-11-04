"""PD-141 portable Principled compatibility subset; explicitly not BSDF conformance."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.existing:
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_OPENPBR_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'))
 with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'openpbr.json').read_text());c={x['stage']:x for x in d['captures']}
def library(stage):return c[stage]['document']['shader_instances']
def profile(stage):return next(iter(library(stage)['instances']))['parameters'].get('openpbr')
def native(stage):return {name:value['openpbr']for name,value in c[stage]['shader_nodes'].items()}
assert profile('principled')is None and profile('restored')is None
assert all(not value['installed']for value in native('principled').values())
for stage in ['openpbr-defaults','lobes','redo','film-thin-wall','undo-film','base-weight','saved','opened']:
 assert profile(stage)and all(value['installed']for value in native(stage).values()),stage
 assert c[stage]['normal_artist_row_absent']and c[stage]['unsupported_disabled']
for material,values in native('lobes').items():
 sockets=values['sockets'];assert abs(sockets['Specular IOR Level'][0]-.8)<1e-6
 assert sockets['Coat Weight']==[1]and abs(sockets['Coat Roughness'][0]-.12)<1e-6
 assert max(abs(a-b)for a,b in zip(sockets['Coat Tint'],[.3,.5,.8,1]))<1e-6
 assert values['base_weight']==[1,1,1,1]
assert profile('invalid-rejected')==profile('lobes')and c['invalid-rejected']['error_visible']
assert profile('undo')==profile('openpbr-defaults')and profile('redo')==profile('lobes')
assert profile('undo-film')==profile('lobes')
for value in native('film-thin-wall').values():
 assert abs(value['sockets']['Thin Film Thickness'][0]-300)<1e-4
 assert value['sockets']['Thin Wall']==[True]
for value in native('base-weight').values():assert abs(value['base_weight'][0]-.2)<1e-6
assert native('restored')==native('principled')and native('opened-restored')==native('principled')
assert library('saved')==library('opened')and native('saved')==native('opened')
assert profile('shader-defaults')==profile('openpbr-defaults')and profile('undo-defaults')==profile('saved')
assert all(x['decimal']=='.'and not x['shader_failures']for x in c.values())
assert all(value['openpbr_idempotent']for stage in c.values()for value in stage['shader_nodes'].values())
differences={}
for before,after in [('principled','lobes'),('lobes','film-thin-wall'),('lobes','base-weight')]:
 images=[Image.open(a.output/(name+'.png')).convert('RGB')for name in [before,after]]
 assert all(max(ImageStat.Stat(image).stddev)>2 for image in images)
 differences[before+'/'+after]=sum(ImageStat.Stat(ImageChops.difference(*images)).mean)/765;assert differences[before+'/'+after]>1e-5
assert c['principled']['view']['scene_view_matrix']==c['restored']['view']['scene_view_matrix']
restored_delta=sum(ImageStat.Stat(ImageChops.difference(Image.open(a.output/'principled.png').convert('RGB'),Image.open(a.output/'restored.png').convert('RGB'))).mean)/765
assert restored_delta<.001,restored_delta
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
assert not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_STACK_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',log,re.I)
result={'pid':d['pid'],'stages':len(c),'scope':'Explicit portable Principled compatibility subset, NOT OpenPBR BSDF conformance. PD-141 remains partial: full lobes/new-project/USD/export integration pending.','scene_linear_color':True,'mapped_native_sockets':True,'identical_parameters_do_not_invalidate_shader':True,'thin_film_micrometers_to_nanometers':True,'undo_redo':True,'unsupported_and_invalid_values_rejected':True,'original_socket_defaults_and_links_restored':True,'save_open_and_legacy_workflow':True,'normal_channel_not_exposed':True,'image_differences':differences,'restored_same_camera_frame_mean_rgb_difference':restored_delta,'clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
