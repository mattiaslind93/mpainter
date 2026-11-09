# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent native neighborhood kernels and witnessed cached edit/undo on a private RTX surface.
Run inside bts-build after reserving the GPU lane; this is not a whole-material CPU oracle.
"""
import os,pathlib,sys,json,subprocess,hashlib,argparse
parser=argparse.ArgumentParser();parser.add_argument('--output',type=pathlib.Path,required=True);parser.add_argument('--binary',type=pathlib.Path,required=True);parser.add_argument('--environment',type=pathlib.Path,required=True);args=parser.parse_args();source=pathlib.Path(__file__).resolve().parents[2];base=pathlib.Path(os.environ.get('BTS_ROOT',str(source.parent))).resolve();binary=args.binary.resolve();assert binary.is_relative_to(base/'build') and binary.is_file();out=args.output.resolve();assert out.is_relative_to(base/'build') and not out.exists();out.mkdir();sys.path.insert(0,str(source/'scripts/host'));from private_gpu_gui import private_gpu_gui
hdri=args.environment.resolve();assert hdri.is_relative_to(base/'build');assert hashlib.sha256(hdri.read_bytes()).hexdigest()=='48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031'
env=dict(os.environ);env.update(BTS_ROOT=str(base),BTS_ENVIRONMENT=str(hdri),BTS_GPU_NEIGHBORHOOD_FILTER_TEST='1',BTS_SESSION_LOG_DIR=str(out/'logs'))
with private_gpu_gui(out,env,source) as (prefix,private):
 command=prefix+['nice','-n','19',str(binary),'--no-splash','--headless-viewport-test',str(out),'--environment',str(hdri),'--library-dir',str(out/'library'),'--recovery-dir',str(out/'recovery')];(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
 with (out/'run.log').open('w') as log:result=subprocess.run(command,env=private,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
 (out/'exit.json').write_text(json.dumps({'returncode':result.returncode},indent=2)+'\n');assert result.returncode==0,result.returncode
proof=json.loads((out/'neighborhood-filters.json').read_text());assert proof['cases']==96 and proof['maximum_error']<=3e-5
witnesses=[record for record in proof['records'] if record['case']=='boundary-mutation-witness'];assert len(witnesses)==8
assert all(w['source_revision_changed'] and w['stamp_x0']==255 and w['stamp_x1']==256 and w['output_x']>=256 and w['output_change']>1e-5 for w in witnesses)
receipt={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':proof['cases'],'maximum_error':proof['maximum_error'],'boundary_mutation_witnesses':witnesses,'scope':proof['scope'],'whole_material_cpu_parity_claimed':False}
(out/'verified.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
