# SPDX-License-Identifier: GPL-3.0-or-later
"""Native New -> nonmodal baking -> skew tools -> true Normal rebake -> packed reopen."""
from parity_test_environment import private_display
import argparse,hashlib,json,math,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--history',action='store_true',help='Separate legacy Configure/Save settings history acceptance');p.add_argument('--history-multiset',action='store_true',help='Separate two-set AO map consumer Bake/Undo/Redo acceptance');a=p.parse_args();a.output=a.output.resolve();
if a.history and a.history_multiset:p.error('Choose one history profile')
if (a.history or a.history_multiset) and a.output.exists():raise ValueError('Choose a fresh history output; preserve the first result')
a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_SKEW_DEMO='1',BTS_SKEW_HIGH_SOURCE=str(root/'native/tests/fixtures/skew_high.obj'),BTS_SKEW_SETS_LOW=str(root/'native/tests/fixtures/skew_sets_low.obj'),BTS_SKEW_SETS_HIGH=str(root/'native/tests/fixtures/skew_sets_high.obj'))
if a.history_multiset:
 env={key:value for key,value in env.items() if not key.endswith('_DEMO') and not key.startswith('BTS_PERFORMANCE')}
 env.update(BTS_SKEW_DEMO='1',BTS_BAKE_MULTISET_HISTORY_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1')
 if 'DISPLAY' in os.environ:env['DISPLAY']=os.environ['DISPLAY']
 else:env.pop('DISPLAY',None)
 private=['config','data','cache','state','resources','recovery','sessions','cuda-cache','gl-cache','tmp']
 for key,name in zip(['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','BTS_RESOURCE_CACHE','BTS_RECOVERY_DIR','BTS_SESSION_LOG_DIR','CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH','TMPDIR'],private):
  directory=a.output/name;directory.mkdir();env[key]=str(directory)
 env['__GL_SHADER_DISK_CACHE']='1'
 sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
 originals={str(path):sha(path) for path in [root/'native/tests/fixtures/skew_sets_low.obj',root/'native/tests/fixtures/skew_fixture.mtl']}
 command=['nice','-n','19','bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/skew_sets_low.obj'),'--gui-fix-resolution','128']
 admission=dict(command=command,source_sha256=originals,verifier_sha256=sha(__file__),environment={key:value for key,value in env.items() if key.startswith(('BTS_','XDG_','CUDA_','__GL_','QT_')) or key in ('DISPLAY','XAUTHORITY','LANG','LC_ALL','TMPDIR')},private_roots_empty=all(not any((a.output/name).iterdir()) for name in private),performance_claim=False)
 (a.output/'multiset-history-admission.json').write_text(json.dumps(admission,indent=2)+'\n')
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=240)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
 unchanged=all(sha(path)==value for path,value in originals.items())
 (a.output/'multiset-history-exit.json').write_text(json.dumps(dict(pid=proc.pid,actual_exit=code,source_unchanged=unchanged),indent=2)+'\n')
 assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
 assert unchanged and admission['private_roots_empty']
 proof=json.loads((a.output/'multiset-history.json').read_text());assert proof['pid']==proc.pid and proof['ao_bake_resolution']==32 and proof['both_sets_without_selection'] is True and proof['performance_claim'] is False
 names=['seeded','before-bake','baked','undone','redone'];assert [row['stage'] for row in proof['captures']]==names
 c={row['stage']:row for row in proof['captures']};f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
 uid=c['seeded']['selected_uid'];identities=None;components=0;maximum=0.
 for name in names:
  snapshot=c[name];sets=snapshot['map_history_sets'];assert len(sets)==2 and len({row['uid'] for row in sets})==2 and snapshot['selected_uid']==uid==sets[0]['uid']
  assert snapshot['source_obj_hash']==c['seeded']['source_obj_hash'] and snapshot['cpu_materializations']==c['seeded']['cpu_materializations']
  assert not snapshot['composite_pending'] and not snapshot['property_pending'] and not snapshot['shader_failures'] and snapshot['skew']['auto_role']==-1
  # Existing native Image identities/format/dimensions, NOT GPU texture handles.
  images=[row for row in snapshot['residency']['images'] if row['material_uid'] in {item['uid'] for item in sets} and row['image'].endswith(' Base Color')]
  assert len(images)==2 and all(row['width']==128 and row['height']==128 and row['bytes_per_texel']==16 for row in images)
  current=sorted((row['material_uid'],row['image_uid'],row['format'],row['width'],row['height']) for row in images)
  if identities is None:identities=current
  else:assert current==identities
  for index,row in enumerate(sets):
   assert row['precision']=='RGB32F' and row['map_size']==[32,32] and len(row['probes'])==3 and row['pixel_hash']==row['descriptor_hash']
   for probe in row['probes']:
    raw=probe['raw'];gpu=probe['gpu'];assert len(raw)==len(gpu)==4 and all(isinstance(v,(int,float)) and math.isfinite(v) for v in raw+gpu) and raw[3]==1
    assert probe['map_xy']==[int(f32(f32(x+.5)/128)*32) for x in probe['xy']]
    expected=max(0.,min(1.,f32(f32(f32(f32(raw[0]/raw[3])-.5)*1)+.5)))
    if name in ('seeded','before-bake','undone'):assert raw[0]==(.25 if index==0 else .75)
    for value in gpu:
     error=abs(value-expected);assert error<=3e-5,(name,row['uid'],probe['xy'],value,expected);maximum=max(maximum,error);components+=1
 assert c['undone']['document']==c['before-bake']['document'] and c['redone']['document']==c['baked']['document']
 base=c['before-bake']['undo_count'];assert [c[name]['undo_count']-base for name in names[1:]]==[0,1,0,1]
 assert c['undone']['redo_count']==1 and c['redone']['redo_count']==0
 for index in range(2):assert c['baked']['map_history_sets'][index]['pixel_hash']!=c['before-bake']['map_history_sets'][index]['pixel_hash']
 recipe=c['baked']['document']['bake_configuration'];assert recipe['resolution']==recipe['height']==32 and recipe['rays']==8 and recipe['project_mesh'] and recipe['enabled']==[False,False,False,True,False,False,False]
 checks=proof['synchronous_history_checks'];assert set(checks)=={'undoDocument','redoDocument'}
 for value in checks.values():assert value['padding_calls_before']==value['padding_calls_after'] and value['materializations_before']==value['materializations_after']
 text=(a.output/'run.log').read_text();assert 'PD150_MULTISET_HISTORY PASS' in text and f'BTS_SHUTDOWN clean pid={proc.pid}' in text
 assert not re.search(r'PD150_MULTISET_HISTORY_FAILED|BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|OpenColorIO Error|BTS_GPU_STACK_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',text,re.I)
 result=dict(pid=proc.pid,actual_ao_bake_resolution=32,set_count=2,selected_set_unchanged=True,both_consumers_fresh_before_undo=True,one_undo_redo_restores_both_maps_and_recipe=True,sampled_gpu_components=components,max_scalar_residual=maximum,assertion_bound=3e-5,native_image_identity_unchanged=True,gpu_texture_handle_parity_claim=False,cpu_layer_composite=False,performance_claim=False)
 (a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(0)
if a.history:
 env={key:value for key,value in env.items() if not key.endswith('_DEMO') and not key.startswith('BTS_PERFORMANCE')}
 env.update(BTS_SKEW_DEMO='1',BTS_BAKE_SETTINGS_HISTORY_DEMO='1',BTS_NATIVE_MAPPING_UPSTREAM='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1')
 if 'DISPLAY' in os.environ:env['DISPLAY']=os.environ['DISPLAY']
 else:env.pop('DISPLAY',None)
 for key,name in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','sessions'),('CUDA_CACHE_PATH','cuda-cache'),('__GL_SHADER_DISK_CACHE_PATH','gl-cache')]:
  directory=a.output/name;directory.mkdir();env[key]=str(directory)
 env['__GL_SHADER_DISK_CACHE']='1'
 source_files=[root/'native/tests/fixtures/skew_low.obj',root/'native/tests/fixtures/skew_fixture.mtl']
 sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
 original={str(path):sha(path) for path in source_files}
 command=['nice','-n','19','bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/skew_low.obj'),'--gui-fix-resolution','128']
 (a.output/'history-admission.json').write_text(json.dumps(dict(command=command,source_sha256=original,environment={key:value for key,value in env.items() if key.startswith(('BTS_','XDG_','CUDA_','__GL_','QT_')) or key in ('DISPLAY','XAUTHORITY','LANG','LC_ALL')},private_caches_empty=all(not any((a.output/name).iterdir()) for name in ['cache','cuda-cache','gl-cache']),performance_claim=False),indent=2)+'\n')
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=240)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
 (a.output/'history-exit.json').write_text(json.dumps(dict(pid=proc.pid,actual_exit=code,source_unchanged=original=={str(path):sha(path) for path in source_files}),indent=2)+'\n')
 assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
 assert original=={str(path):sha(path) for path in source_files}
 proof=json.loads((a.output/'configure-history.json').read_text());assert proof['pid']==proc.pid and proof['actual_controls'] is True and proof['normal_bake_resolution']==32 and proof['performance_claim'] is False
 names=['unpainted','painted','baking-empty','unchanged-save','settings-saved','painting','undo-mode','undo-settings','cache-old','undo-entry','empty-noop-save','redo-entry','redo-settings','cache-new','redo-painting','branch-baking','branch-old','branch-saved','cancelled','cancel-cache','branch-undone','branch-redone','baked','bake-undone','bake-old-cache','bake-redone','bake-new-cache','finished']
 assert [row['stage'] for row in proof['captures']]==names
 c={row['stage']:row for row in proof['captures']}
 def recipe(name):return c[name]['document'].get('bake_configuration',{})
 def layers(name):return c[name]['document']['texture_sets'][0]['layers']
 def maps(name):return c[name]['document']['texture_sets'][0]['mesh_maps']
 def admission(name):return {key:c[name][key] for key in ('document','document_revision','undo_count','redo_count')}
 assert not recipe('unpainted') and not recipe('undo-entry')
 assert layers('unpainted')!=layers('painted')
 expected_undo=[0,1,2,2,3,4,3,2,2,1,1,2,3,3,4,3,2,3,3,3,2,3,4,3,3,4,4,5]
 baseline=c['unpainted']['undo_count']
 assert [c[name]['undo_count']-baseline for name in names]==expected_undo
 for before,after in [('baking-empty','unchanged-save'),('undo-settings','cache-old'),('undo-entry','empty-noop-save'),('redo-settings','cache-new'),('branch-saved','cancelled'),('cancelled','cancel-cache'),('bake-undone','bake-old-cache'),('bake-redone','bake-new-cache')]:assert admission(before)==admission(after),(before,after)
 for before,after in [('baking-empty','undo-settings'),('baking-empty','redo-entry'),('settings-saved','redo-settings'),('painting','redo-painting'),('branch-old','branch-undone'),('branch-saved','branch-redone'),('branch-redone','bake-undone'),('baked','bake-redone')]:assert recipe(before)==recipe(after),(before,after)
 assert recipe('settings-saved')['frontal']==.125 and recipe('branch-saved')['frontal']==.25 and c['branch-saved']['redo_count']==0
 for name in names[2:]:
  assert layers(name)==layers('painted') and c[name]['source_obj_hash']==c['painted']['source_obj_hash'],name
  assert c[name]['skew']['auto_role']==-1 and not c[name]['skew']['pending'] and not c[name]['shader_failures'],name
 for name in ['painting','redo-painting','undo-entry','empty-noop-save','finished']:assert c[name]['skew']['mode']==0,name
 for name in ['baking-empty','undo-mode','undo-settings','redo-entry','redo-settings','branch-saved','bake-undone','bake-redone']:assert c[name]['skew']['mode']==1,name
 assert maps('settings-saved')==maps('painted') and maps('branch-redone')==maps('bake-undone')
 assert maps('baked')==maps('bake-redone') and maps('baked')[1:]==maps('branch-redone')[1:]
 normal=maps('baked')[0];assert normal and normal['tiles'] and normal!=maps('branch-redone')[0]
 assert recipe('baked')['resolution']==32 and recipe('baked')['frontal']==.375
 assert all(plane['resolution']==32 and plane['height']==32 and len(plane['hash'])==16 for plane in normal['tiles'])
 choices=proof['dialog_choices'];assert [row['operation'] for row in choices]==['noop','save','inspect','noop','inspect','save','cancel','inspect','bake','inspect','inspect']
 assert [row['displayed'] for row in choices]==[.01,.125,.01,.01,.125,.25,.5,.25,.375,.25,.375] and all(row['actual_visible_control'] for row in choices)
 text=(a.output/'run.log').read_text();assert 'PD150_CONFIGURE_HISTORY PASS' in text and f'BTS_SHUTDOWN clean pid={proc.pid}' in text
 assert not re.search(r'PD150_CONFIGURE_HISTORY_FAILED|BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|OpenColorIO Error|BTS_GPU_STACK_FALLBACK',text,re.I)
 result=dict(pid=proc.pid,real_paint_then_mode_history=True,configure_save_one_history=True,cancel_and_noop_exact=True,empty_recipe_cache_restore=True,reopened_controls_match_undo_redo=True,branch_clears_redo=True,completed_normal_bake_recipe_and_maps_undo_redo=True,original_source_unchanged=True,performance_claim=False)
 (a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(0)
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/skew_low.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as f:
 proc=subprocess.Popen(command,env=env,stdout=f,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=900)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
proof=json.loads((a.output/'skew.json').read_text());assert proof['pid']==proc.pid;c={v['stage']:v for v in proof['captures']}
for key in ['project-source-disabled','average-disabled','cage-disabled']:assert not c[key]['paint_button_enabled'],key
for value in c.values():assert value['decimal']=='.' and not value['shader_failures'] and value['document']['schema']==int(re.search(r'texture_document_schema\s*=\s*(\d+)',(root/'native/host/document/TextureDocument.hh').read_text()).group(1))
assert not c['initial']['skew']['settings_visible']
assert c['initial']['skew']['mode']==2 and c['initial']['skew']['eligible'] and c['initial']['skew']['completed']>=1
assert c['initial']['skew_normal_hash']!=c['painted-uv']['skew_normal_hash']
assert c['initial']['skew_hash']!=c['painted-uv']['skew_hash']!=c['painted-3d']['skew_hash']
assert c['erased']['skew_hash']!=c['painted-3d']['skew_hash'] and c['undo-erase']['skew_hash']==c['painted-3d']['skew_hash'] and c['redo-erase']['skew_hash']==c['erased']['skew_hash']
for before,after in [('initial','painted-uv'),('painted-uv','painted-3d'),('painted-3d','erased')]:assert c[after]['undo_count']==c[before]['undo_count']+1,(before,after)
assert c['polygon']['skew_hash']!=c['erased']['skew_hash']
assert c['edges-off']['skew_normal_hash']!=c['edges-on']['skew_normal_hash']
assert c['vectors']['skew']['vectors']>c['edges-on']['skew']['vectors'] and c['vectors']['skew']['completed']==c['edges-on']['skew']['completed']
assert not c['disabled']['skew_metadata']['enabled'] and c['enabled']['skew_metadata']['enabled']
assert c['painting-mode']['skew']['mode']==0 and c['undo-mode']['skew']['mode']==2
assert c['reference']['skew_reference']['exact'] and c['reference']['composite_checked'] and not c['reference']['composite_differences']
for value in c.values():
 if value['stage'].startswith('sets-'):continue
 assert value['source_obj_hash']==c['initial']['source_obj_hash'],value['stage']
 assert value['document']['texture_sets'][0]['layers']==c['initial']['document']['texture_sets'][0]['layers'],value['stage']
assert c['manual-before']['skew']['completed']==c['reference']['skew']['completed']
assert c['manual-baked']['skew_normal_hash']!=c['manual-before']['skew_normal_hash']
assert c['manual-baked']['undo_count']==c['manual-before']['undo_count']+1
assert c['undo-manual']['skew']['mode']==1 and c['undo-manual']['skew_normal_hash']==c['manual-before']['skew_normal_hash']
assert c['redo-manual']['skew_normal_hash']==c['manual-baked']['skew_normal_hash']
assert c['edges-on']['vector_colored_pixels']>1000 and c['vectors']['vector_colored_pixels']>2*c['edges-on']['vector_colored_pixels']
assert c['saved']['skew_hash']==c['opened']['skew_hash'] and c['saved']['skew_normal_hash']==c['opened']['skew_normal_hash']
assert c['opened']['skew_metadata']['tiles'] and c['opened']['skew']['mode']==2
first=c['sets-initial']['document']['texture_sets'];second=c['sets-second']['document']['texture_sets'];finished=c['sets-finished']['document']['texture_sets']
assert len(first)==len(second)==len(finished)==2
assert second[0]['skew_correction']['tiles'] and second[0]['mesh_maps'][0]!=first[0]['mesh_maps'][0]
assert not second[1]['skew_correction']['tiles'] and second[1]['mesh_maps'][0]
assert finished[0]['skew_correction']==second[0]['skew_correction'] and finished[0]['mesh_maps'][0]==second[0]['mesh_maps'][0]
assert finished[1]['skew_correction']['tiles'] and finished[1]['mesh_maps'][0]!=second[1]['mesh_maps'][0]
for key in ['skew_correction','mesh_maps']:
 assert [v[key] for v in c['sets-saved']['document']['texture_sets']]==[v[key] for v in c['sets-opened']['document']['texture_sets']]
assert proof['stroke_max_ping_ms']<100,proof['stroke_max_ping_ms']
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',nonmodal_baking=True,eligibility_checks=True,uv_and_3d_brush=True,eraser_polygon=True,one_undo_per_stroke=True,shared_mode_undo=True,edge_protection_changes_normals=True,manual_bake_undo=True,per_set_queued_rebakes=True,visible_vectors=True,vector_settings=True,original_mesh_uv_layers_unchanged=True,partial_full_reference_exact=True,nonempty_packed_reopen=True,max_stroke_ping_ms=proof['stroke_max_ping_ms']);(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
