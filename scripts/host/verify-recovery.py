# SPDX-License-Identifier: GPL-3.0-or-later
"""Kill only our own native Qt/Blender PID during an actual .blend@ write; verify both complete files."""
import argparse,atexit,hashlib,json,os,signal,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();repo=Path(__file__).resolve().parents[2];root=repo.parent;a.output.mkdir(parents=True,exist_ok=True);recovery=a.output/'snapshots'
def command(folder,mode,path,latest=False):
    folder.mkdir(exist_ok=True);args=['bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--project',str(root/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(folder),'--recovery-dir',str(recovery),mode,str(path)]
    if latest:args.append('--recover-latest-demo')
    return args
owned=[]
def spawn(*args,**kwargs):
    child=subprocess.Popen(*args,**kwargs);owned.append(child);return child
def cleanup():
    for child in owned:
        if child.poll() is None:child.kill();child.wait()
atexit.register(cleanup)
crash=a.output/'crash';target=crash/'last-good.blend';temporary=Path(str(target)+'@');observed=0;children=set();started=time.monotonic()
with (a.output/'crash.log').open('w') as log:
    process=spawn(command(crash,'--recovery-crash-demo',a.source),stdout=log,stderr=subprocess.STDOUT)
    while process.poll() is None:
        child=Path(f'/proc/{process.pid}/task/{process.pid}/children')
        try:native=Path(f'/proc/{process.pid}/exe').resolve(strict=True).name=='bts-host'
        except FileNotFoundError:native=False
        if native and child.exists():children.update(child.read_text().split())
        if (crash/'witness.json').exists() and temporary.exists():
            try:observed=temporary.stat().st_size
            except FileNotFoundError:observed=0
            if observed>=65536:
                process.send_signal(signal.SIGSTOP);time.sleep(.02)
                assert temporary.exists(),'Native rename won the interruption race; retry with a larger workload'
                process.kill();process.wait(timeout=10);break
        if time.monotonic()-started>900:
            process.kill();process.wait();raise RuntimeError('Recovery crash phase timed out')
        time.sleep(.001)
assert process.returncode==-signal.SIGKILL and observed>=65536,(process.returncode,observed)
assert not children,children
witness=json.loads((crash/'witness.json').read_text());assert witness['pid']==process.pid
assert hashlib.sha256(target.read_bytes()).hexdigest()==witness['last_good_sha256'],'Interrupted save changed the last good project'
snapshots=sorted(recovery.glob('*.blend'),key=lambda p:p.stat().st_mtime_ns);assert len(snapshots)==2;latest=snapshots[-1];assert latest.with_suffix('.blend.json').exists()
proof={'crashed_pid':process.pid,'interrupted_native_temporary_bytes':observed,'last_good_sha256_preserved':True,'two_complete_rotating_snapshots':True,'no_runtime_child_processes':True}
for label,path,expected,use_picker in [('last-good',target,witness['baseline'],False),('recovered',latest,witness['edited'],True)]:
    output=a.output/label
    with (a.output/f'{label}.log').open('w') as log:
        opened=spawn(command(output,'--recovery-open-demo',path,use_picker),stdout=log,stderr=subprocess.STDOUT);opened.wait(timeout=900)
    assert opened.returncode==0,(label,opened.returncode)
    text=(a.output/f'{label}.log').read_text();assert 'BTS_SHUTDOWN clean' in text and 'Not freed memory blocks' not in text
    demo=json.loads((output/'paint.json').read_text());assert demo['pid']==opened.pid
    captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','opened','painted','undo']
    assert captures['opened']['document']==expected,(label,'wrong document')
    assert captures['painted']['document']!=expected and captures['undo']['document']==expected,(label,'not editable/undoable')
    value=captures['opened']['recovery']['center_roughness'];base=witness['baseline']['texture_sets'][0]['base_values']['roughness']
    # The soft brush need not reach full .8 at this sample. Exact sparse-page hashes above
    # compare every authored value with the pre-crash witness; this checks visible change.
    assert (value>base+.1 and value<=.800001) if use_picker else abs(value-base)<1e-6,(label,value)
    assert captures['opened']['recovery']['unsaved_path']==use_picker
    proof[label]={'pid':opened.pid,'document_exact':True,'center_roughness':value,'further_paint_and_undo':True,'unsaved_recovered_project':use_picker}
auto_output=a.output/'idle-autosave';auto_root=a.output/'idle-snapshots'
with (a.output/'idle-autosave.log').open('w') as log:
    args=command(auto_output,'--recovery-auto-demo',target);args[args.index('--recovery-dir')+1]=str(auto_root);args.extend(['--autosave-seconds','1']);idle=spawn(args,stdout=log,stderr=subprocess.STDOUT);idle.wait(timeout=900)
assert idle.returncode==0
log=(a.output/'idle-autosave.log').read_text();assert 'BTS_RECOVERY_READY' in log and 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
captures={c['stage']:c for c in json.loads((auto_output/'paint.json').read_text())['captures']};assert list(captures)==['initial','opened','painted','autosaved','snapshot-opened','painted-again','undo']
assert captures['opened']['document']==witness['baseline']
assert captures['painted']['document']!=captures['opened']['document']
assert captures['autosaved']['document']==captures['painted']['document']==captures['snapshot-opened']['document']==captures['undo']['document']
assert captures['painted-again']['document']!=captures['snapshot-opened']['document']
assert captures['autosaved']['recovery']['dirty'] and not captures['autosaved']['recovery']['unsaved_path']
assert captures['snapshot-opened']['recovery']['unsaved_path'] and len(list(auto_root.glob(witness['baseline']['id']+'-*.blend')))<=2
proof['idle_autosave']={'pid':idle.pid,'dirty_state_and_original_project_path_preserved':True,'actual_idle_snapshot_reopened_exactly':True,'further_edit_and_undo':True,'bounded_to_two_snapshots':True}
assert hashlib.sha256(target.read_bytes()).hexdigest()==witness['last_good_sha256']
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
