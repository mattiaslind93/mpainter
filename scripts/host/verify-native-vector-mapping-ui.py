"""Own VectorMath extra-POINT warm UI and two independent source-hidden cold children.

Run inside bts-build at nice19 against a frozen host. Fixture generation is a
separate explicit background Blender step; this wrapper never changes a graph.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from private_gpu_gui import private_gpu_gui

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--expected-binary-sha256', required=True)
a = parser.parse_args()
root = Path(__file__).resolve().parents[2]
bts = Path(os.environ.get('BTS_ROOT', root.parent)).resolve()
out = a.output.resolve()
binary = Path(os.environ['BTS_HOST_EXECUTABLE']).resolve()
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS, 0) == 19
assert out.is_relative_to((bts / 'build').resolve()) and not out.exists()

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

assert sha(binary) == a.expected_binary_sha256
fixture = a.fixture.resolve()
manifest = json.loads(fixture.with_suffix('.json').read_text())
assert manifest['kind'] == 'owned-retained-vector-math-v1' and manifest['images'] == 0
assert manifest['materials'] == ['AW Vector Add', 'AW Vector Multiply'] and manifest['operations'] == ['ADD', 'MULTIPLY']
# Pin all native sources used by this executable, and the new actor/runtime tools.
source_hashes = {str(p.relative_to(root)): sha(p) for p in sorted((root/'native/host').glob('*')) if p.is_file()}
for name in ['native/tests/native_vector_mapping_fixture.py', 'scripts/host/verify-native-vector-mapping-ui.py', 'scripts/host/private_gpu_gui.py']:
    source_hashes[name] = sha(root/name)
out.mkdir(parents=True)
fixtures = out/'fixtures'; fixtures.mkdir()
source = fixtures/'source.blend'; source.write_bytes(fixture.read_bytes())
mesh = fixtures/'plane.obj'
mesh.write_text('o AW_Vector_Owner\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
environment = Path(os.environ.get('BTS_ENVIRONMENT', bts/'build/shared-fixtures/TT_environmentLights_v001.hdr')).resolve()
assert environment.is_file()
result = {'complete': False, 'binary_sha256': sha(binary), 'source_hashes': source_hashes,
          'fixture_sha256': sha(fixture), 'fixture_manifest_sha256': sha(fixture.with_suffix('.json')),
          'environment_sha256': sha(environment), 'phases': [],
          'scope': 'One real warm Add+Multiply UI process and two independent fresh packed Open processes; 128x128 one-tile functional test, no latency or data-before-first-render claim.'}

def save():
    (out/'receipt.json').write_text(json.dumps(result, indent=2)+'\n')

def check_sources():
    assert all(sha(root/name) == value for name,value in source_hashes.items())
    assert sha(binary) == result['binary_sha256']

def phase(name, witness=None):
    check_sources()
    directory = out/name; directory.mkdir()
    library = directory/'library'; library.mkdir()
    env = {k:v for k,v in os.environ.items() if not k.startswith(('BTS_', 'XDG_', 'BLENDER_', 'QT_')) and k not in ('DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD')}
    env.update(BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary), BTS_ENVIRONMENT=str(environment),
               BTS_AW_VECTOR_UI='1', BTS_BJ_PROJECTION_UI='1', BTS_AW_VECTOR_SOURCE=str(source),
               BTS_SESSION_LOG_DIR=str(directory/'logs'), BLENDER_USER_CONFIG=str(directory/'blender'),
               CUDA_CACHE_PATH=str(directory/'cuda'), __GL_SHADER_DISK_CACHE_PATH=str(directory/'nvidia'))
    for folder in ['blender','cuda','nvidia']:(directory/folder).mkdir()
    if witness:
        assert not source.exists() and not fixtures.exists()
        expected=json.loads(witness.read_text())
        assert sha(Path(expected['project'])) == expected['project_sha256']
        env.update(BTS_AW_VECTOR_COLD='1',BTS_AW_VECTOR_EXPECTED=str(witness))
    command = ['bash', str(root/'scripts/host/run-host.sh'), '--no-splash', '--workspace',
               '--gui-fix-demo', str(directory), '--gui-fix-mesh', str(mesh), '--gui-fix-resolution','128',
               '--autosave-seconds','3600','--library-dir',str(library)]
    receipt={'complete':False,'actual_exit':None,'binary_sha256':sha(binary),'source_hashes':source_hashes,
             'command':command,'cold':bool(witness),'source_hidden':not source.exists()}
    if witness:receipt['witness_sha256']=sha(witness)
    def write(): (directory/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    write(); start=time.monotonic()
    with private_gpu_gui(directory/'private',env,root) as (prefix, child), (directory/'run.log').open('w') as log:
        assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir()) and not list(library.iterdir())
        receipt['initial_resource_cache_empty']=True
        process=subprocess.Popen(prefix+command,cwd=root,env=child,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        receipt['pid']=process.pid; write()
        try:
            while process.poll() is None:
                if time.monotonic()-start > 270:
                    receipt['timeout_seconds']=270;os.killpg(process.pid,signal.SIGTERM);break
                # No broad prune: refuse to continue if the shared build budget is exhausted.
                allocated=int(subprocess.check_output(['du','-s','-B1',str(bts/'build')],text=True).split()[0])
                if allocated >= 49_800_000_000:
                    receipt['storage_guard_bytes']=allocated;os.killpg(process.pid,signal.SIGTERM);break
                time.sleep(2)
            try:code=process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL);code=process.wait()
        finally:
            if process.poll() is None:os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=15)
    receipt.update(actual_exit=code,seconds=time.monotonic()-start);write()
    text=(directory/'run.log').read_text()
    assert code==0, receipt
    assert 'BTS_SHUTDOWN clean' in text and 'BTS_AW_VECTOR_UI_PASS' in text
    assert not any(x in text for x in ['BTS_HOST_FAILED','BTS_GL_ERROR','BTS_BRUSH_CPU_FALLBACK','Not freed memory blocks'])
    proof=json.loads((directory/'native-vector-ui.json').read_text())
    assert proof['complete'] and proof['cold']==bool(witness)
    assert len(proof['checkpoints'])==(1 if witness else 2)
    assert len(proof['pickers'])==(1 if witness else 8)  # two real Select mesh pickers added
    for picker in proof['pickers']:
        assert picker['active'] and picker['field_focus'] and picker['selected_files']==[picker['path']]
        assert all(picker[x]==1 for x in ['pressed','released','clicked','accepted'])
    for checkpoint in proof['checkpoints']:
        s=checkpoint['state']; assert s['tiles']==[1001] and s['width']==s['height']==128
        assert s['cpu_materializations']==s['cpu_layer_compositions']==0
        assert len(s['gpu'])==2 and len(s['lower'])>=1 and s['source_sha256']
        assert sha(Path(checkpoint['project']))==checkpoint['project_sha256']
    analytics=[x for x in proof['checks'] if x['stage']=='analytic']
    assert len(analytics)==(1 if witness else 10)
    assert all(x['max_error']<3e-5 and x['bound']==3e-5 for x in analytics)
    check_sources()
    receipt.update(complete=True,clean_shutdown=True,proof_sha256=sha(directory/'native-vector-ui.json'))
    write(); result['phases'].append({'name':name,'receipt':str(directory/'receipt.json'),'sha256':sha(directory/'receipt.json'),'pid':receipt['pid']});save()

save()
phase('warm')
# Hide the exact file/mesh directory used by the warm import. Native archives
# must restore from their packed bytes. Never clear caches in a live process.
hidden=out/'fixtures-hidden';fixtures.rename(hidden)
try:
    phase('cold-Add',out/'warm/witness-Add.json')
    phase('cold-Multiply',out/'warm/witness-Multiply.json')
finally:
    assert not fixtures.exists();hidden.rename(fixtures)
assert len({x['pid'] for x in result['phases']})==3
check_sources();result['complete']=True;save();print(json.dumps(result,indent=2))
