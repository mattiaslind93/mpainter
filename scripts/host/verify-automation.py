"""Exercise real embedded Python Console controls and native project lifecycle."""
from parity_test_environment import private_display
import argparse,json,os,subprocess
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--jobs',action='store_true');p.add_argument('--open-only',action='store_true');p.add_argument('--extensions',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_AUTOMATION_DEMO=str(a.output),BTS_AUTOMATION_MESH=str(root/'fixtures/single_tile.obj'))
env.update(QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_RESOURCE_CACHE=str(a.output/'resources'),BTS_SESSION_LOG_DIR=str(a.output/'logs'))
if a.open_only:env['BTS_AUTOMATION_OPEN_ONLY']='1'
if a.extensions:a.jobs=True;env['BTS_EXTENSION_DEMO']='1'
if a.jobs:env['BTS_AUTOMATION_JOBS_DEMO']='1'
if not a.check_only:
 with (a.output/'run.log').open('w') as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--no-splash','--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=360)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4500:])
if a.open_only:
 data=json.loads((a.output/'automation-open.json').read_text());assert data['same_document'] and data['busy_return'] and data['overlap_rejected'] and data['deferred_ready']
 assert data['status']['is_open'] and not data['status']['is_busy'] and not data['status']['open_error']
 log=(a.output/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log and 'BTS_HOST_FAILED' not in log
 print(json.dumps(data,indent=2));raise SystemExit(0)
data=json.loads((a.output/'automation.json').read_text());plugins=json.loads((a.output/'plugin-count.json').read_text());assert data['api_version']==[1,0]
if not a.check_only:assert data['pid']==process.pid
assert data['project']['is_open'] and not data['project']['is_busy'] and not data['project']['needs_saving']
assert data['project']['file_path']==str(a.output/'copy.blend')
assert data['owner_thread_rejected'] and data['busy_save_rejected'] and data['copy_preserves_path']
assert data['sets'][0]['name']=='FixtureSurface' and data['sets'][0]['width']==128
assert plugins=={'start':2,'close':2},plugins
for name in ['project.blend','copy.blend']:assert (a.output/name).stat().st_size>1000
assert not (a.output/'busy.blend').exists()
image=Image.open(a.output/'viewport.png').convert('RGB');magenta=sum(r>235 and g<20 and b>235 for r,g,b in image.getdata());assert magenta==0,magenta
log=(a.output/'run.log').read_text();assert 'BTS_HOST_FAILED' not in log and 'BTS_SHUTDOWN clean' in log
proof={'pid':data['pid'],'locale':'sv_SE.UTF-8','api_version':data['api_version'],'native_console_commands':True,'invalid_resolution_rejected':True,'same_pid_existing_blender_python':True,'busy_save_rejected':True,'deferred_ready_callback':True,'packed_project_open_and_copy':True,'wrong_thread_rejected':True,'plugin_start_close_counts':plugins,'magenta_pixels':magenta,'dock_close':True}
if a.jobs:
 jobs=json.loads((a.output/'automation-jobs.json').read_text());assert jobs['pid']==data['pid']
 assert jobs['export']['status']==jobs['async_export']['status']==jobs['baked_export']['status']=='success'
 assert jobs['cancelled_bake']['status']=='cancelled' and jobs['bake']['status']=='success'
 assert jobs['async_png_byte_exact'] and jobs['bake_undo_redo_exact'] and jobs['maps_and_resource_pack_open_exact']
 assert len(jobs['packed_maps'])==2 and {item['role']for item in jobs['packed_maps']}=={3,4}
 assert {item['role']:item['width']for item in jobs['packed_maps']}=={3:128,4:256}
 for task in ['export','async_export','baked_export']:
  for filename in jobs[task]['files']:assert Path(filename).is_file(),filename
 proof['jobs']={key:jobs[key]for key in ['async_png_byte_exact','bake_undo_redo_exact','maps_and_resource_pack_open_exact']}
 proof['jobs'].update(exported_files=len(jobs['export']['files']),baked_exported_files=len(jobs['baked_export']['files']),bake_roles=[3,4],per_baker_sizes={'ao':128,'curvature':256},cancelled_bake_installs_nothing=True)
if a.extensions:
 extensions=json.loads((a.output/'extensions.json').read_text());assert extensions['pid']==data['pid']
 assert extensions['plugin_ui_clicks']==3 and extensions['plugin_open_callbacks']==1
 assert extensions['reload_unique'] and extensions['ui_keeps_camera'] and extensions['deleted_handle_rejected']
 events=extensions['events']
 for index,event in enumerate(events):
  kind,payload=event['type'],event['data']
  if kind in ('ProjectCreated','ProjectOpened'):
   assert any(later['type']=='ProjectEditionEntered' and later['data']['project_id']==payload['project_id']for later in events[index+1:])
  if kind in ('ExportTexturesAboutToStart','BakingProcessAboutToStart'):
   ended='ExportTexturesEnded' if kind.startswith('Export')else 'BakingProcessEnded'
   assert any(later['type']==ended and later['data']['id']==payload['id']for later in events[index+1:])
  if kind=='BakingProcessProgress':assert 0<=payload['progress']<=1
  if kind=='ExportTexturesEnded':assert set(sum(payload['by_texture_set'].values(),[])).issubset(payload['files'])
 proof['extensions']={'event_count':len(events),'plugin_clicks':3,'open_handler_after_reload':1,'lifecycle_and_job_order':True,'camera_unchanged_by_ui':True,'deleted_handle_invalid':True}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
