"""Private RTX proof for four original SPSM materials, five bakes and real OBJ/FBX New.
Run inside bts-build after reserving the GPU lane. No proprietary graph evaluation.
"""
import argparse,hashlib,json,os,pathlib,re,shutil,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--format',choices=['obj','fbx'],required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--fixtures',type=pathlib.Path,required=True);p.add_argument('--binary',type=pathlib.Path,required=True);p.add_argument('--environment',type=pathlib.Path,required=True);a=p.parse_args()
source=pathlib.Path(__file__).resolve().parents[2];base=pathlib.Path(os.environ.get('BTS_ROOT',str(source.parent))).resolve();binary=a.binary.resolve();assert binary.is_relative_to(base/'build') and binary.is_file();out=a.output.resolve();assert out.is_relative_to(base/'build');assert not out.exists(),out
out.mkdir();inputs=out/'inputs';inputs.mkdir();fixture=a.fixtures.resolve()
for path in fixture.glob('*.spsm'):shutil.copyfile(path,inputs/path.name)
assert {path.name for path in inputs.glob('*.spsm')}=={'dirty-plastic-robot.spsm','shd-hard-rust.spsm','sratched-metal-pealing-paint-dirt.spsm','worn leather chest.spsm'}, 'Requires all four original comparison materials'
mesh=inputs/('test_object.'+a.format);shutil.copyfile(fixture/mesh.name,mesh)
if a.format=='obj':shutil.copyfile(fixture/'test_object.mtl',inputs/'test_object.mtl')
hdri=a.environment.resolve();assert hdri.is_relative_to(base/'build');assert hashlib.sha256(hdri.read_bytes()).hexdigest()=='48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031'
sys.path.insert(0,str(source/'scripts/host'));from private_gpu_gui import private_gpu_gui
original={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs.glob('*.spsm')}
env=dict(os.environ);env.update(BTS_ROOT=str(base),BTS_ENVIRONMENT=str(hdri),BTS_SPSM_DEMO='1',BTS_SKEW_DEMO='1',BTS_SPSM_HIDE_SOURCES='1',BTS_FLATTEN_VERIFY_OUTPUT='1',BTS_PERFORMANCE_PROFILE='1',BTS_SESSION_PROFILE='1',BTS_SESSION_LOG_DIR=str(out/'logs'))
with private_gpu_gui(out,env,source) as (prefix,private):
 private['BTS_SPSM_FIXTURES']=str(inputs);private['BTS_RESOURCE_CACHE']=str(out/'cache')
 command=prefix+['nice','-n','19',str(binary),'--workspace','--environment',str(hdri),'--gui-fix-demo',str(out),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(out/'library'),'--recovery-dir',str(out/'recovery')]
 (out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
 with (out/'run.log').open('w') as log:process=subprocess.run(command,env=private,stdout=log,stderr=subprocess.STDOUT,timeout=4800)
 (out/'exit.json').write_text(json.dumps({'returncode':process.returncode},indent=2)+'\n');assert process.returncode==0,process.returncode
text=(out/'run.log').read_text(errors='replace');assert 'BTS_SHUTDOWN clean' in text;assert not re.search(r'Shader compil(?:ation|e) failed|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_GPU_AUTHORING_CPU_READ',text,re.I),text[-4000:]
proof=json.loads((out/'spsm-import.json').read_text());stages={c['stage']:c for c in proof['captures']};assert proof['appearance_verified'] is False and 'proof_scope' in proof and proof['bake_resolution']==512 and proof['original_inputs_hidden_during_open'] and proof['original_source_cache_removed_before_open'];assert len(proof['source_files'])==4
assert len(stages['baked-five-maps']['spsm_baked_maps'])==5
assert len(proof['gpu_flattened_exports'])==4
for export in proof['gpu_flattened_exports']:
 assert export['files'] and export['gpu_source_checks'];assert all('_normal.' not in file for file in export['files'])
 assert all(check['covered_components']>0 and check['maximum_error']<=check['tolerance'] for check in export['gpu_source_checks'])
for i in range(4):
 capture=stages['applied-'+str(i)];assert capture['spsm_oracle_scope']['status'] in ['passed','unavailable'];assert len(capture['spsm_baked_maps'])==5;assert all(x['covered']>0 for x in capture['spsm_baked_maps']);assert capture['spsm_bump_origin']==.5
 assert any(x['native_generators'] for x in capture['spsm_native_layers']);assert sum(x['visible'] for x in capture['spsm_native_layers'])>1
 assert stages['redo-'+str(i)]['document']==capture['document']
assert stages['saved']['document']==stages['reopened']['document']
assert not inputs.exists() and (out/'inputs.held').is_dir()
assert original=={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in (out/'inputs.held').glob('*.spsm')}
summary={'format':a.format,'real_mesh_new':True,'materials':4,'resolution':512,'baked_roles':[1,3,4,5,6],'private_display':json.loads((out/'private-display.json').read_text()),'source_files_unchanged':True,'nonmodal_shelf_apply':True,'undo_redo':True,'packed_reopen_without_inputs_or_cache':True,'painter_pixel_parity_claimed':False,'appearance_verified':False,'whole_material_cpu_parity':{name:stage.get('spsm_oracle_scope') for name,stage in stages.items() if name.startswith('applied-') or name=='reopened'},'gpu_flattened_exports':proof['gpu_flattened_exports']}
summary['binary_sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
phases={}
for flow,phase,wall,cpu,queue,ui in re.findall(r'BTS_PERFORMANCE_PROFILE flow=(.*?) phase=(\S+) wall_ms=(\S+) cpu_ms=(\S+) queue_ms=(\S+) ui=(\d+)',text):
 if not flow.startswith('spsm_'):continue
 key=(flow,phase);entry=phases.setdefault(key,{'material_flow':flow,'phase':phase,'calls':0,'maximum_wall_ms':0,'maximum_queue_ms':0,'ui':bool(int(ui))});entry['calls']+=1;entry['maximum_wall_ms']=max(entry['maximum_wall_ms'],float(wall));entry['maximum_queue_ms']=max(entry['maximum_queue_ms'],float(queue))
summary['material_lifecycle_timings']=list(phases.values());summary['performance_scope']='Observed four-material lifecycle timings, including export/diagnostic waits; separate strict AA benchmark required'
(out/'verified.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
