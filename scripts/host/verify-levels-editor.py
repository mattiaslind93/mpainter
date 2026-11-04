"""Native shared Levels/Filter ramps, GPU histogram, one Undo per drag and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_LEVELS_EDITOR_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:
 subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
d=json.loads((a.output/'levels-native.json').read_text());c={x['stage']:x for x in d['captures']}
for before,edited,undone,redone in [('levels-default','levels-output-drag','levels-undo','levels-redo'),('filter-default','filter-output-drag','filter-undo','filter-redo')]:
 assert c[edited]['undo_count']==c[before]['undo_count']+1
 assert c[before]['effects']==c[undone]['effects'] and c[edited]['effects']==c[redone]['effects']
 assert len(c[before]['roughness_gpu'])==len(c[edited]['roughness_gpu'])==4
 assert abs(c[before]['roughness_gpu'][0]-c[edited]['roughness_gpu'][0])>.03
 assert c[before]['roughness_gpu']==c[undone]['roughness_gpu'] and c[edited]['roughness_gpu']==c[redone]['roughness_gpu']
assert abs(c['levels-output-drag']['effects'][0]['output_low']-.2)<1e-6
assert abs(c['filter-output-drag']['effects'][-1]['filter']['output_high']-.7)<1e-6
assert abs(c['levels-output-drag']['roughness_gpu'][0]-(.2+.8*c['levels-default']['roughness_gpu'][0]))<.008
assert abs(c['filter-output-drag']['roughness_gpu'][0]-.7*c['filter-default']['roughness_gpu'][0])<.008
for stage in ['levels-default','levels-output-drag','histogram-color','histogram-roughness']:
 assert c[stage]['histogram_ready'] and sum(c[stage]['histogram_bins'][0])>0
assert c['histogram-color']['histogram_bins']!=c['histogram-roughness']['histogram_bins']
assert c['reopened']['effects']==c['filter-redo']['effects'] and c['reopened']['roughness_gpu']==c['filter-redo']['roughness_gpu']
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
assert not re.search(r'BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'native_qt_drags':True,'one_undo_per_30_move_drag':True,'gpu_roughness_pixels_undo_redo':True,'gpu_histogram_follows_channel':True,'packed_open':True,'captures':list(c)}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
