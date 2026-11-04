"""Native Blender shader-node codegen on a private offscreen GPU context."""
import argparse,json,os,subprocess,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cases',type=Path);p.add_argument('--material',action='append');p.add_argument('--displacement-fixtures',action='store_true');p.add_argument('--legacy-cache-repro',action='store_true');p.add_argument('--legacy-tree-repro',action='store_true');p.add_argument('--decode-images',action='store_true');p.add_argument('--archive-roundtrip',action='store_true');p.add_argument('--external-images',action='store_true');p.add_argument('--host-queue',action='store_true');p.add_argument('--host-activation',action='store_true');p.add_argument('--host-multi-object',action='store_true');p.add_argument('--host-capture',action='store_true');p.add_argument('--capture-baseline-repro',action='store_true');p.add_argument('--reference',type=Path);p.add_argument('--timeout',type=float,default=120);a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to((bts/'build').resolve());out=out/'legacy'if a.legacy_cache_repro else out;out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_NATIVE_MATERIAL_GPU_TEST=str(out),BTS_GL_DEBUG='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
# The legacy stack suite checks the opt-in GPU storage ledger.
if env.get('BTS_VERIFY_GPU_STACK'):env.setdefault('BTS_PERFORMANCE_PROFILE','1')
if a.legacy_cache_repro:env['BTS_NATIVE_MATERIAL_CACHE_LEGACY']='1'
if a.decode_images:env['BTS_NATIVE_MATERIAL_DECODE_IMAGES']='1'
if a.archive_roundtrip:env['BTS_NATIVE_MATERIAL_ARCHIVE_TEST']='1'
cases=json.loads(a.cases.read_text())if a.cases else [];cases=[v for v in cases if not a.material or v['material']in a.material]
if a.displacement_fixtures:
 for name,height,mid,scale,space,object_scale,custom in [('negative',-.25,.5,2,'object',1,False),('hdr-object',.25,0,5,'object',2,False),('hdr-world',.25,0,5,'world',2,False),('custom-normal',.25,0,5,'world',1,True)]:cases.append({'material':name,'object_scale':object_scale,'synthetic_displacement':{'height':height,'midlevel':mid,'scale':scale,'space':space,'custom_normal':custom}})
if cases:
 manifest=out/'gpu-cases.json';manifest.write_text(json.dumps(cases));env['BTS_NATIVE_MATERIAL_CASES']=str(manifest)
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
if a.external_images:env['BTS_NATIVE_MATERIAL_EXTERNAL_IMAGES']='1'
if a.host_capture or a.capture_baseline_repro:env['BTS_NATIVE_CAPTURE_TEST']='1'
if a.capture_baseline_repro:env['BTS_NATIVE_CAPTURE_BASELINE']='1'
if a.host_multi_object:env['BTS_NATIVE_MULTI_OBJECT_TEST']='1'
if a.host_activation or a.host_multi_object:env['BTS_NATIVE_MATERIAL_ACTIVATION_TEST']='1'
if a.host_queue or a.host_activation or a.host_multi_object or a.host_capture or a.capture_baseline_repro:env['BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST']='1';env['BTS_NATIVE_PREPARATION_TRACE']='1'
if a.legacy_tree_repro:env['BTS_NATIVE_TREE_LEGACY']='1'
if a.legacy_cache_repro:env['XDG_CACHE_HOME']=str(out.parent/'cache')
with(out/'run.log').open('w')as log:native_result=subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--no-splash','--headless-viewport-test',str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=False,timeout=a.timeout)
(out/'native-exit.json').write_text(json.dumps({'actual_exit':native_result.returncode},indent=2)+'\n')
log=(out/'run.log').read_text()
if a.capture_baseline_repro:
 assert 'BTS_NATIVE_CAPTURE_COLOR_BASELINE gpu_packet=1 owners=2'in log
 assert 'BTS_NATIVE_CAPTURE_PLAN normal=0 bump=1 native_normal_supported=0 native_bump_supported=0'in log
 assert 'Native bump/normal extraction is not implemented yet'in log and 'BTS_SHUTDOWN clean'in log and 'Not freed memory blocks'not in log
 (out/'baseline-reproduced.json').write_text(json.dumps({'native_color_gpu_capture_passed':True,'unavailable_bump_capture_requested':True,'internal_normal_not_requested':True,'production_fix_applied':False},indent=2)+'\n')
 print('Native Flatten baseline: valid native Color GPU packet, then unavailable linked-Bump requested');raise SystemExit(0)
if a.legacy_tree_repro:
 assert 'BTS_NATIVE_TREE_ISOLATION callbacks=0 expected=0 legacy=1'in log and 'BTS_SHUTDOWN clean'in log and 'BTS_HOST_FAILED'not in log and 'Not freed memory blocks'not in log
 (out/'legacy-tree-repro.json').write_text(json.dumps({'independent_worker_update_skipped':True,'callbacks':0,'expected':1},indent=2)+'\n');print('Legacy process-global guard silently skipped an independent Main update');raise SystemExit(0)
if a.legacy_cache_repro:
 rows=re.findall(r'BTS_NATIVE_BINDING_ORACLE binding=(\d+) actual=(\S+) expected=(\S+) key=(\d+)',log);assert len(rows)==2 and rows[1][1]!=rows[1][2] and 'Native binding cache alias reproduced'in log and 'Not freed memory blocks'not in log
 (out/'verified.json').write_text(json.dumps({'legacy_cache_alias_reproduced':True,'rows':rows},indent=2)+'\n');print('Legacy missing binding namespace: incorrect second parameter reproduced');raise SystemExit(0)
assert native_result.returncode==0, json.loads((out/'native-exit.json').read_text())
if env.get('BTS_VERIFY_GPU_STACK'):
 match=re.search(r'BTS_GPU_STACK_SELF_TEST cases=(\d+).*precision=pass',log);assert match and int(match[1])>=6879
 probes=re.findall(r'BTS_GPU_STACK_VERIFIED channel=\d+ probes=64 max_error=\S+ valid=(\d+)',log);assert probes and all(v=='1'for v in probes)
assert 'BTS_HOST_FAILED'not in log, log[-8000:]
assert 'BTS_NATIVE_MATERIAL_GPU constant=pass'in log and 'BTS_GL_ERROR'not in log and 'BTS_SHUTDOWN clean'in log and 'Not freed memory blocks'not in log
if a.host_queue or a.host_activation or a.host_multi_object:
 host_queue=json.loads((out/'native-host-queue.json').read_text());assert host_queue['ready'] and host_queue['frames']>1 and host_queue['visible_windows']==0
 if a.host_activation or a.host_multi_object:assert host_queue['document_activation']['packed_save_open'] and host_queue['document_activation']['reopen_maximum_error']<1e-6
proof=json.loads((out/'native-material-gpu.json').read_text());assert proof['unsupported_rejected_without_callback_exception']
if a.host_capture:
 host_queue=json.loads((out/'native-host-queue.json').read_text());proof['native_capture']=host_queue['native_capture'];capture=proof['native_capture'];assert capture['objects']==2 and capture['tiles']==[1001,1002] and min(capture['owner_samples'])>1000 and capture['component_checks']>100000 and capture['maximum_error']<3e-5
 assert capture['native_caps_resolved'] and capture['pre_new_source_space_stable'] and capture['legacy_archive_space_preserved'] and capture['legacy_hydrated_gpu_oracle'] and capture['signed_hdr_source_color'] and capture['unavailable_bump_rejected'] and capture['unavailable_bump_export_omitted'] and capture['available_bump_grayscale'] and capture['internal_normal_preserved'] and not capture['normal_exported'] and capture['packet_tail'] and capture['mask_and_opacity'] and capture['cancel_keeps_source'] and capture['source_free_packed_open'] and capture['gpu_composite_oracle'] and capture['cpu_native_composites']==0 and capture['exported_files']
 assert capture['standard_visible_native_rejected'] and capture['standard_flatten_export_gpu_oracle'] and capture['standard_packed_open_export_gpu_oracle'] and capture['standard_exported_files']==8
assert proof['surface_program']['multi_object_owner_table'] and proof['surface_program']['multi_object_texel_checks']>=2000
if a.host_multi_object:
 assert host_queue['document_activation']['source_objects']>1
 multi=host_queue['multi_object'];assert len(multi['cases'])==3 and multi['covered_texel_checks']>100000 and multi['maximum_error']<3e-5 and multi['gpu_storage_samples'] and not multi['cpu_composition']
 assert multi['reference_object_rejected'] and multi['instancer_rejected'] and multi['unused_object_reference_uv_allowed'] and multi['unused_normal_reference_color_allowed']
 for case in multi['cases']:assert case['objects']==2 and case['tiles']==[1001,1002] and min(case['owner_a_samples'],case['owner_b_samples'])>1000 and case['live_instance_edits'] and case['snapshot_undo_redo'] and case['packed_source_free_reopen'] and case['parameter_metadata_exact'] and case['shader_recompiles']==0
stack=proof['native_stack'];assert stack['maximum_error']<2e-6 and stack['per_channel_invalidation'] and stack['folder_cache'] and stack['uv_transform_udim'] and not stack['cpu_composition']
surface=proof['surface_program'];assert len(surface['attributes'])==9 and surface['tiles']==[1001,1002,1011] and surface['max_error']<2e-6 and surface['shader_recompiles']==0 and surface['borrowed_geometry'] and surface['dirty_region_preserved'] and surface['varying_native_noise'] and surface['missing_geometry_rejected'] and surface['fresnel_view_context'] and surface['color_space_conversion'] and surface['baked_ao_context'] and surface['native_object_view_context'] and surface['linked_ramp_runtime_copy'] and surface['dynamic_ramp_instances'] and surface['preparation_queue_latest_wins'] and surface['prepared_before_activation'] and surface['tree_worker_isolation'] and surface['shared_native_cpu_image'] and surface['shared_native_gpu_image'] and surface['bounded_image_upload'] and surface['image_upload_steps']>2 and surface['shared_library_instances']
assert proof['native_nodes'] and proof['maximum_error']<1e-6 and proof['synthetic_readbacks']==6 and proof['diagnostic_readbacks']>=8 and len(proof['binding_cases'])==2 and proof['binding_cases'][0]['cache_key']!=proof['binding_cases'][1]['cache_key'] and proof['shader_recompiles']==0 and 0<proof['native_noise']<1 and proof['signed_parameter']
if cases:
 assert len(proof['real_groups'])==len(cases)
 for group in proof['real_groups']:
  if a.archive_roundtrip:assert group['archive']['materials']==1 and group['archive']['input_interface_exact'] and all(v.get('external',False)if a.external_images else v['packed']for v in group['archive']['images']if v['required'])
  if a.external_images:assert group['archive']['project_resource_pack_roundtrip'] and group['archive']['original_snapshot_cache_removed']
  if a.decode_images:assert group['worker_image_guard']
  assert group['source_inputs_preserved'] and all(v['channel']in (0,1,2,14)for v in group['channels'])
  if group.get('synthetic_displacement')and group['synthetic_displacement']['custom_normal']:assert group['unsupported_channels'][0]['channel']==14 and 'custom displacement normal'in group['unsupported_channels'][0]['error']
  else:assert len(group['channels'])==4
if a.reference:
 reference=json.loads(a.reference.read_text());reference=reference.get('gpu',reference);previous={g['material']:g for g in reference['real_groups']}
 for group in proof['real_groups']:
  original={c['channel']:c for c in previous[group['material']]['channels']}
  for channel in group['channels']:assert max(abs(x-y)for x,y in zip(channel['rgba'],original[channel['channel']]['rgba']))<1e-6,(group['material'],channel,original[channel['channel']])
 proof['source_archive_gpu_exact']=True
(out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
