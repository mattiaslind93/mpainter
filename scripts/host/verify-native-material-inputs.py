"""Read explicit artist-selected libraries in a detached native Main, without OpenGL."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--crafty',type=Path,required=True);p.add_argument('--clay',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to((bts/'build').resolve());out.mkdir(parents=True,exist_ok=True)
names=['4k-Cardboard-Damaged','4k-Clay','4k-Stylized-Stone','Particle-Confetti','Particle-Sprinkle'];cases=[{'file':str(a.crafty.resolve()),'material':name}for name in names]+[{'file':str(a.clay.resolve()),'material':'mClay_005'}]
manifest=out/'cases.json';manifest.write_text(json.dumps(cases));env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_NATIVE_MATERIAL_CASES=str(manifest),LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
sdk=bts/'qt/6.8.3';env.update(QT_PLUGIN_PATH=str(sdk/'plugins'),LD_LIBRARY_PATH=str(sdk/'lib')+(':'+env['LD_LIBRARY_PATH']if env.get('LD_LIBRARY_PATH')else ''))
with(out/'run.log').open('w')as log:subprocess.run([os.environ.get('BTS_HOST_EXECUTABLE',str(bts/'build/host/bin/bts-host')),'--no-splash','--cpu-native-material-test',str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
assert 'Not freed memory blocks'not in (out/'run.log').read_text()
data=json.loads((out/'native-material-inspection.json').read_text());assert len(data['cases'])==6 and data['cancelled_request_rejected'] and not data['gpu_required']
assert len(data['single_file_libraries'])==2 and all(v['source_files']==1 and v['materials']>0 for v in data['single_file_libraries'])
types=set();ramps=0;mapping=0
for case in data['cases']:
 assert case['surface_type']=='ShaderNodeGroup' and case['source_unchanged'] and case['group'] and case['parameters'],case
 assert len({v['id']for v in case['parameters']})==len(case['parameters'])
 if 'channels'in case:
  assert case['original_inputs_preserved'];channels={v['channel']:v for v in case['channels']};assert all(channels[c]['prepared'] for c in (0,1,2,5)),case
  assert case['parameter_buffer_entries']==len(case['parameters']) and all(v['prepared']or v['error']for v in case['channels'])
 resource=case["native_resource_pipeline"];assert "native-channel:0" in resource["supported_channels"]
 assert resource["bytes"]>0 and resource["renamed_source_preserved"] and resource["shared_encoded_bytes"]
 for v in case['parameters']:types.add(v['type']);ramps+=bool(v['ramp']);mapping+=v['mapping']
assert types=={0,1,2} and ramps==2 and mapping>=3,(types,ramps,mapping)
data['status']='native-input-inspection-verified; GPU channel evaluation/UI integration still pending';data['input_types']=sorted(types);data['ramps']=ramps;data['mapping_inputs']=mapping
(out/'verified.json').write_text(json.dumps(data,indent=2)+'\n');print(json.dumps({'cases':[(c['material'],len(c['parameters']),c['milliseconds'])for c in data['cases']],'ramps':ramps,'mapping':mapping},indent=2))
