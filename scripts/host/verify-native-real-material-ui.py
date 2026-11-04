"""Runtime-only real Crafty5/mClay artist acceptance. No build or source mutation.

Root must approve the frozen host and release the private GPU lane separately.
Each case uses actual controls at128², independent causal/restoration witnesses.
The original3GB library is read in place and never copied/renamed/rewritten.
"""
from pathlib import Path
import argparse,hashlib,json,math,os,signal,subprocess,time
from private_gpu_gui import private_gpu_gui

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--binary',type=Path,default=os.environ.get('BTS_HOST_EXECUTABLE'))
p.add_argument('--expected-binary-sha256',required=True)
p.add_argument('--cases',type=Path,default=Path(__file__).resolve().parents[2]/'native/tests/native_real_material_cases.json')
p.add_argument('--case',choices=('cardboard','clay','stone','confetti','sprinkle','mclay'))
p.add_argument('--environment',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=root.parent;out=a.output.resolve();binary=Path(a.binary).resolve()
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS,0)==19
# Keep large packed material witnesses outside the shared build-size budget.
# Both roots are dedicated test output; artist/project directories are excluded.
assert any(out.is_relative_to((bts/name).resolve()) for name in ('build','test-runs')) and not out.exists()

def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def pinned_stat(path):
    s=Path(path).stat();return {'size':s.st_size,'mtime_ns':s.st_mtime_ns,'inode':s.st_ino,'device':s.st_dev}
def require(ok,message):
    if not ok:raise RuntimeError(message)
def no_other_host():
    names=('bts-host','blender','native-height-images','bts-fragment-diagnostic')
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():continue
        try:name=(entry/'exe').readlink().name
        except OSError:continue
        require(name not in names,'Another application owns the GPU lane: '+name)
def descendants(pid):
    nodes={}
    for p in Path('/proc').iterdir():
        if not p.name.isdecimal():continue
        try:s=(p/'stat').read_text();v=s[s.rfind(')')+2:].split();nodes[int(p.name)]={'parent':int(v[1]),'start':v[19]}
        except (FileNotFoundError,ProcessLookupError,PermissionError):pass
    selected={pid}
    while True:
        before=len(selected);selected.update(n for n,r in nodes.items() if r['parent'] in selected)
        if before==len(selected):return {n:nodes[n] for n in selected if n in nodes}
def terminate(pid):
    owned=descendants(pid)
    for sig in (signal.SIGTERM,signal.SIGKILL):
        for n,r in sorted(owned.items(),reverse=True):
            try:
                s=Path(f'/proc/{n}/stat').read_text();v=s[s.rfind(')')+2:].split()
                if v[19]==r['start']:os.kill(n,sig)
            except (FileNotFoundError,ProcessLookupError,PermissionError):pass
        if sig==signal.SIGTERM:time.sleep(3)
    return owned

assert sha(binary)==a.expected_binary_sha256
case_path=a.cases.resolve();table=json.loads(case_path.read_text());require(table['kind']=='owned-real-native-material-ui-v1','Unexpected case manifest kind')
rows=table['cases'];require([x['id'] for x in rows]==['cardboard','clay','stone','confetti','sprinkle','mclay'],'Six exact real cases required')
require(sha(root/table['inspection_proof'])==table['inspection_proof_sha256'],'Reviewed actual inspection metadata changed')
inspection=json.loads((root/table['inspection_proof']).read_text())['cases']
for row,original in zip(rows,inspection):
    require(row['material']==original['material'] and row['source']==original['source'] and row['group']==original['group'],'Explicit source/material does not match actual inspected source')
    require(Path(row['source']).is_absolute(),'Only explicit absolute source paths allowed')
selected=[row for row in rows if a.case is None or row['id']==a.case]
source_files={str(p.relative_to(root)):sha(p) for p in sorted((root/'native/host').glob('*')) if p.is_file()}
for name in ['scripts/host/verify-native-real-material-ui.py','scripts/host/private_gpu_gui.py','scripts/host/run-host.sh','native/tests/native_real_material_cases.json',table['inspection_proof']]:source_files[name]=sha(root/name)
case_sha=sha(case_path);environment=a.environment.resolve();require(sha(environment)=='48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031','Approved HDRI changed')
# Pin existing source metadata, not a copied substitute. No actor changes data files.
libraries={row['source']:pinned_stat(row['source']) for row in selected}
out.mkdir();mesh=out/'plane.obj';mesh.write_text('o AW_Real_Owner\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
grid={'tile':1001,'origin':[4,4],'step':[8,8],'shape':[16,16],'samples_per_channel':256,'order':'row-major y then x'}
result={'diagnostic_grid':grid,'complete':False,'actual_exit':None,'binary_sha256':sha(binary),'case_manifest':str(case_path),'case_manifest_sha256':case_sha,'source_hashes':source_files,'library_metadata_before':libraries,'environment_sha256':sha(environment),'cases':[],'scope':'Actual explicit real-material128²UI, causal GPU0/2 changes and exact restoration; no synthetic affine/material beauty oracle, fresh-cold claim, active Bump/custom Normal or full AW/latency claim.'}
def save():(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
def guard():
    require(sha(binary)==a.expected_binary_sha256 and sha(case_path)==case_sha,'Frozen ELF/case manifest changed')
    require(all(sha(root/n)==v for n,v in source_files.items()),'Frozen source changed')
    require(all(pinned_stat(n)==v for n,v in libraries.items()),'Original real library changed')
    size=int(subprocess.check_output(['du','-s','-B1',str(bts/'build')],text=True).split()[0]);require(size<49_800_000_000,'Shared build storage exceeds49.8GB')
save();active=None
try:
    for row in selected:
        guard();no_other_host();d=out/row['id'];d.mkdir();env={k:v for k,v in os.environ.items() if not k.startswith(('BTS_','XDG_','BLENDER_','QT_')) and k not in ('DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD')}
        env.update(BTS_ROOT=str(bts),BTS_HOST_EXECUTABLE=str(binary),BTS_ENVIRONMENT=str(environment),BTS_AW_REAL_UI='1',BTS_BJ_PROJECTION_UI='1',BTS_PERFORMANCE_PROFILE='1',BTS_SESSION_PROFILE='1',BTS_AW_REAL_CASES=str(case_path),BTS_AW_REAL_CASE=row['id'],BTS_SESSION_LOG_DIR=str(d/'logs'),PYTHONDONTWRITEBYTECODE='1')
        for key,folder in [('BLENDER_USER_CONFIG','blender'),('CUDA_CACHE_PATH','cuda'),('__GL_SHADER_DISK_CACHE_PATH','nvidia')]:
            (d/folder).mkdir();env[key]=str(d/folder)
        command=['bash',str(root/'scripts/host/run-host.sh'),'--no-splash','--workspace','--gui-fix-demo',str(d),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(d/'library')]
        record={'case':row['id'],'material':row['material'],'source':row['source'],'complete':False,'actual_exit':None,'known_missing_external_images':row['known_missing_external_images'],'command':command};result['cases'].append(record);save();started=time.monotonic()
        # Display ownership/config/recovery stay in build for existing guards.
        # Large authoring caches and data live beside this dedicated test run.
        control=(bts/'build/aw-real-material-gui-control'/hashlib.sha256(str(out).encode()).hexdigest()/row['id']).resolve()
        require(not control.exists(),'Private display control output must be fresh')
        record['private_display_control']=str(control);save()
        with private_gpu_gui(control,env,root) as (prefix,child),(d/'run.log').open('x') as log:
            for key,folder in [('XDG_CACHE_HOME','cache'),('XDG_DATA_HOME','data'),('TMPDIR','tmp'),('BTS_RESOURCE_CACHE','resources'),('XDG_STATE_HOME','state')]:
                path=d/'private'/folder;path.mkdir(parents=True);child[key]=str(path)
            record['private_output_environment']={key:child[key] for key in ['XDG_CONFIG_HOME','BTS_RECOVERY_DIR','XDG_RUNTIME_DIR','XDG_CACHE_HOME','XDG_DATA_HOME','TMPDIR','BTS_RESOURCE_CACHE','XDG_STATE_HOME']}
            record['private_display_receipt_sha256']=sha(control/'private-display.json');save()
            active=subprocess.Popen(prefix+command,cwd=root,env=child,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);record['pid']=active.pid;save()
            while active.poll() is None:
                guard();require(time.monotonic()-started<270,'Real material native270s timeout');time.sleep(2)
            record.update(actual_exit=active.returncode,seconds=time.monotonic()-started);active=None;save()
        text=(d/'run.log').read_text();record['clean_shutdown']='BTS_SHUTDOWN clean' in text
        if record['actual_exit']!=0:
            if (d/'failure.json').is_file():
                failure=json.loads((d/'failure.json').read_text());record.update(failure_sha256=sha(d/'failure.json'),failure_stage=failure.get('stage'),failure_reason=failure.get('error'))
                if any(name in str(failure.get('error','')) for name in row['known_missing_external_images']):record['blocker']='Actual import reported the known missing external image; no image fabricated and no bypass'
            save();raise RuntimeError('First actual real material failure: '+row['id'])
        require(record['clean_shutdown'] and 'BTS_AW_REAL_UI_PASS' in text,'Missing real native PASS/CLEAN')
        require(not any(x in text for x in ['BTS_HOST_FAILED','BTS_GL_ERROR','BTS_BRUSH_CPU_FALLBACK','BTS_GPU_AUTHORING_CPU_READ','Not freed memory blocks']),'Native failure marker')
        proof=d/'native-real-material-ui.json';packet=json.loads(proof.read_text());require(packet['complete'] and packet['case']==row and len(packet['checkpoints'])==1 and len(packet['pickers'])==4,'Actual real-material UI proof shape differs')
        for picker in packet['pickers']:require(picker['active'] and picker['field_focus'] and picker['selected_files']==[picker['path']] and all(picker[k]==1 for k in ['pressed','released','clicked','accepted']),'Actual picker signal/input admission failed')
        checks=packet['checks'];require(len([x for x in checks if x['stage']=='causal-GPU-change'])==len(row['gestures']),'Missing actual causal controls')
        require(len([x for x in checks if x['stage']=='exact-undo'])==len(row['gestures']) and len([x for x in checks if x['stage']=='exact-redo'])==len(row['gestures']),'Missing exact oneUndo/Redo witnesses')
        witness=d/'native-real-material-witness.json';w=json.loads(witness.read_text());require(w['case']==row and sha(Path(w['project']))==w['project_sha256'],'Packed witness identity mismatch')
        for state in [packet['checkpoints'][0]['state'],w['state']]:
            require(state['tiles']==[1001] and state['width']==state['height']==128 and state['cpu_materializations']==state['cpu_layer_compositions']==0 and len(state['gpu'])==2,'Real material dimensions/GPU/CPU scope differs')
            require(all(state['probe_grid'][k]==v for k,v in grid.items()),'Real-only deterministic grid differs')
            require([q['channel'] for q in state['gpu']]==[0,2] and all(q['tile']==1001 and len(q['values'])==256 and all(len(rgba)==4 and all(math.isfinite(x) for x in rgba) for rgba in q['values']) for q in state['gpu']),'Real GPU grid packet shape/finite contract failed')
        bakes=[x for x in checks if x['stage']=='actual-project-mesh-ao-bake']
        if row.get('prerequisite'):
            require(row['id']=='mclay' and row['prerequisite']=={'kind':'project-mesh-ao-bake','role':3,'tile':1001,'resolution':128,'rays':8,'sampling':1} and len(bakes)==1,'Missing exact mClay actual AO Bake prerequisite')
            bake=bakes[0]['bake'];require(all(bake.get(k)==1 for k in ['start_pressed','start_released','start_clicked','settings_accepted','progress_accepted','finish_clicked','completed_tasks','total_tasks']),'Actual AO Bake start/completion signals differ')
            require(bake['enabled']==[False,False,False,True,False,False,False] and bake['project_mesh'] and bake['resolution']==bake['height']==128 and bake['rays']==8 and bake['sampling']==1,'Actual AO Bake options differ')
            map=bake['map'];require(map['role']==3 and len(map['tiles'])==1,'Actual AO map role/tile count differs');plane=map['tiles'][0]
            require(plane['tile']==1001 and plane['width']==plane['height']==128 and plane['pixels_present'] and len(plane['hash'])==16 and plane['hash']==plane['storage_hash'],'Actual AO map metadata/hash missing')
            for state in [packet['checkpoints'][0]['state'],w['state']]:require(state['real_mesh_maps']==[map] and state['real_bake_configuration']==bake['recipe'],'Actual packed Open lost baked AO map/recipe')
        else:require(not bakes,'Unexpected Bake prerequisite on another real material')
        color_gestures=[x for x in row['gestures'] if x['kind'] in ('color','ramp-color')]
        color_inputs=[x for x in checks if x['stage']=='actual-color-hex'];color_references=[x for x in checks if x['stage']=='independent-color-metadata']
        require(len(color_inputs)==len(color_references)==len(color_gestures),'Missing actual color input or independent reference')
        for gesture,entered,reference in zip(color_gestures,color_inputs,color_references):
            require(entered['entered_hex'].casefold()==entered['displayed_hex'].casefold()==gesture['hex'].casefold(),'Actual committed hex does not match request')
            require(reference['requested_hex'].casefold()==gesture['hex'].casefold() and reference['named_input_space'] and reference['named_canonical_space']=='ACEScg','Explicit named color spaces missing')
            require(reference['owner_context']['working_space']==reference['owner_context']['active_working_space']=='ACEScg' and reference['owner_context']['native_acescg'],'Actual owner context not canonical ACEScg')
            require(len(reference['recorded_color_from_aces'])==9 and len(reference['expected_source_RGB'])==len(reference['absolute_errors'])==3 and reference['actual_source_RGBA'][3]==1,'Color reference shape/alpha differs')
            require(reference['absolute_bound']==2e-6 and math.isfinite(reference['maximum_error']) and reference['maximum_error']<=reference['absolute_bound'],'Independent color metadata bound failed')
        guard();record.update(complete=True,proof_sha256=sha(proof),witness=str(witness),witness_sha256=sha(witness),binary_sha256=sha(binary));save()
    guard();no_other_host();result.update(complete=True,actual_exit=0,library_metadata_unchanged=True);save()
except BaseException as error:
    if active is not None and active.poll() is None:
        result['cases'][-1]['terminated_owned_descendants']=terminate(active.pid);result['cases'][-1]['actual_exit']=active.wait(timeout=15)
    result.update(actual_exit=1,error=repr(error));save();raise
print(json.dumps(result,indent=2))
