"""Real offscreen Qt channel controls, GPU storage retirement and packed history."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_LAYER_CHANNEL_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:
 subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/two_tile_udim.obj'),'--gui-fix-resolution','512','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'layer-channels.json').read_text());c={x['stage']:x for x in d['captures']}
assert c['defaults']['channel_flags']==[True,True,True,True,False]
assert not c['defaults']['disp_button'] and not c['defaults']['disp_settings']
assert c['painted']['channel_pages'][0]>0 and c['painted']['channel_pages'][3]>0
for label in ['color-off','color-redo']:
 assert c[label]['channel_flags'][0] is False and c[label]['channel_pages'][0]==0
assert c['color-undo']['channel_flags'][0] and c['color-undo']['channel_pages']==c['painted']['channel_pages']
assert c['color-empty-on']['channel_flags'][0] and c['color-empty-on']['channel_pages'][0]==0
assert c['disp-on']['disp_button'] and c['disp-on']['disp_settings']
assert not c['disp-off']['disp_button'] and not c['disp-off']['disp_settings']
assert c['reopened']['channel_flags']==c['disp-off']['channel_flags'] and c['reopened']['channel_pages']==c['disp-off']['channel_pages']
assert c['fill-defaults']['channel_flags']==[True,True,True,True,False]
assert not c['fill-rough-off']['channel_flags'][2] and not c['fill-rough-redo']['channel_flags'][2]
assert c['fill-rough-off']['fill_rough_row_hidden'] and c['fill-rough-redo']['fill_rough_row_hidden']
assert not c['fill-rough-undo']['fill_rough_row_hidden']
assert c['fill-rough-undo']['channel_flags'][2] and c['fill-reopened']['channel_flags']==c['fill-rough-off']['channel_flags']
def close(a,b):return len(a)==len(b)==2 and all(len(x)==len(y)==4 and max(abs(i-j)for i,j in zip(x,y))<1e-5 for x,y in zip(a,b))
assert not close(c['defaults']['channel_samples'],c['painted']['channel_samples'])
assert close(c['painted']['channel_samples'],c['color-undo']['channel_samples'])
for label in ['color-off','color-redo','color-empty-on','disp-off','reopened']:assert close(c['defaults']['channel_samples'],c[label]['channel_samples']),(label,c[label]['channel_samples'])
log=(a.output/'run.log').read_text();retired=[dict(zip(['channel','source_count','source_bytes','authoring_pages','shared_preserved','before','after'],map(int,m)))for m in re.findall(r'BTS_CHANNEL_RELEASE channel=(\d+) source_count=(\d+) source_bytes=(\d+) authoring_pages=(\d+) shared_preserved=(\d+) storage_before=(\d+) storage_after=(\d+)',log)]
assert any(x['channel']==0 and x['authoring_pages']>0 and x['source_bytes']>0 and x['after']<x['before']for x in retired),retired
assert not re.search(r'BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
result={'captures':list(c),'defaults_color_metal_rough_bump_on_disp_off':True,'two_udim_gpu_pixels_undo_redo':True,'reenable_empty':True,'displacement_controls_one_layer_flag':True,'packed_save_open':True,'gpu_storage_releases':retired,'storage_measurement':'Live allocated GPU texture storage, not driver-reserved physical heap'}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
