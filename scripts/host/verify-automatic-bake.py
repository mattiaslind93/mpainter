"""Native per-map auto baking, coalesced edits, source cache, sync, Undo and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--profile',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_AUTO_BAKE_DEMO='1',BTS_SKEW_DEMO='1',BTS_SKEW_HIGH_SOURCE=str(root/'native/tests/fixtures/skew_high.obj'),BTS_SKEW_SETS_LOW=str(root/'native/tests/fixtures/skew_sets_low.obj'))
if a.profile:env['BTS_PERFORMANCE_PROFILE']='1'
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/skew_low.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=600)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
proof=json.loads((a.output/'automatic-bake.json').read_text());assert proof['pid']==proc.pid;c={s['stage']:s for s in proof['captures']}
def maps(stage,index=0):return c[stage]['document']['texture_sets'][index]['mesh_maps']
def config(stage):return c[stage]['document']['bake_configuration']
def quality(stage,index,role):
 recipe=config(stage);sid=c[stage]['document']['texture_sets'][index]['id'];q={k:recipe.get(k,v) for k,v in dict(dilation=32,rays=64,sampling=1,diffusion=False).items()};q.update(recipe.get('map_options',{}).get('{'+sid.strip('{}')+'}',{}).get(str(role),{}));return q
assert c['initial']['skew']['mode']==1 and c['initial']['skew']['auto_role']==-1 and c['initial']['skew']['bake_eligible'] and not c['initial']['skew']['eligible']
for role in range(7):
 stage=f'baked-{role}';assert maps(stage)[role] and c[stage]['undo_count']==c['initial']['undo_count']+role+1
 if role:assert maps(stage)[:role]==maps(f'baked-{role-1}')[:role]
assert c['ao-auto']['auto_buttons']==[False,False,False,True,False,False,False]
assert c['ao-drag']['undo_count']==c['ao-auto']['undo_count']+1
assert quality('ao-drag',0,3)['rays']==96 and maps('ao-drag')[3]!=maps('ao-auto')[3]
for role in [0,1,2,4,5,6]:assert maps('ao-drag')[role]==maps('ao-auto')[role]
assert c['ao-reference']['skew_reference']['exact'] and c['ao-reference']['skew_reference']['role']==3
assert c['normal-auto']['auto_buttons']==[True,False,False,False,False,False,False]
assert not any(c['auto-off']['auto_buttons']) and maps('off-edited')==maps('auto-off')
assert maps('manual-normal')[0]!=maps('off-edited')[0]
assert c['manual-normal']['undo_count']==c['off-edited']['undo_count']+1
assert c['manual-undo']['skew']['mode']==1 and maps('manual-undo')==maps('off-edited')
assert maps('manual-redo')==maps('manual-normal')
assert not any(c['missing-source']['auto_enabled']) and not any(c['missing-cage']['auto_enabled'])
assert c['external-normal']['skew']['source_loads']==1
assert c['distance-drag']['skew']['source_loads']==1 and abs(config('distance-drag')['frontal']-.3)<1e-6
assert c['distance-drag']['undo_count']==c['external-normal']['undo_count']+1
assert c['distance-reference']['skew_reference']['exact']
assert '512 MiB' in c['budget-error']['skew']['error']
assert c['budget-error-stable']['skew']['completed']==c['budget-error']['skew']['completed'] and not c['budget-error-stable']['skew']['pending']
assert not c['recovered']['skew']['error'] and c['recovered']['skew']['completed']>c['budget-error']['skew']['completed']
assert maps('saved')==maps('opened') and config('saved')==config('opened')
assert quality('sets-before-sync',0,3)['rays']==12 and quality('sets-before-sync',1,3)['rays']==64
assert quality('sets-synced',0,3)==quality('sets-synced',1,3)
assert c['sets-second']['quality_rays']==12
assert quality('sets-independent',0,3)['rays']==12 and quality('sets-independent',1,3)['rays']==24
assert quality('sets-undo-edit',1,3)['rays']==12 and quality('sets-undo-sync',1,3)['rays']==64
assert quality('sets-redo-sync',1,3)['rays']==12 and quality('sets-redo-edit',1,3)['rays']==24
assert c['sets-reference']['skew_reference']['exact']
assert config('sets-saved')==config('sets-opened')
assert [s['mesh_maps'] for s in c['sets-saved']['document']['texture_sets']]==[s['mesh_maps'] for s in c['sets-opened']['document']['texture_sets']]
for value in c.values():
 assert sum(value['auto_buttons'])<=1 and value['decimal']=='.' and not value['shader_failures']
 if not value['stage'].startswith('sets-'):
  assert value['source_obj_hash']==c['initial']['source_obj_hash']
  assert value['document']['texture_sets'][0]['layers']==c['initial']['document']['texture_sets'][0]['layers']
from PIL import Image
for name in ['ao-drag','sets-opened']:
 image=Image.open(a.output/(name+'.png')).convert('RGB')
 assert len(image.getcolors(image.width*image.height))>256,('blank presentation',name)
assert proof['max_ping_ms']<100,proof['max_ping_ms']
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',all_seven_single_map_bakes=True,exactly_one_auto_map=True,auto_ao_changes_only_ao=True,coalesced_drag_one_undo=True,nonblank_window_capture=True,max_ping_ms=proof['max_ping_ms'],auto_off=True,manual_bake_undo_reopens_baking=True,source_cache_no_reimport_during_distance_drag=True,exact_full_reference=True,budget_failure_stops_and_recovers=True,per_set_quality_sync_undo_redo=True,packed_open_exact=True,original_mesh_uv_layers_unchanged=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
