# SPDX-License-Identifier: GPL-3.0-or-later
"""Fresh host restores a custom environment from packed bytes after its source/cache are hidden."""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--demo',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];root=repo.parent;a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'stock.log').open('w') as log:
    subprocess.run(['bash','-c','source "$1"; export WAYLAND_DISPLAY=; exec "$2" --background --factory-startup --disable-autoexec "$3" --python-exit-code 1 --python "$4" -- "$5"',
        'stock-environment',str(repo/'scripts/host/gpu-env.sh'),str(root/'build/baseline/bin/blender'),str(a.demo/'resource.blend'),str(repo/'scripts/host/stock-environment-roundtrip.py'),str(a.output/'stock')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
stock=json.loads((a.output/'stock/stock.json').read_text());assert hashlib.sha256(a.source.read_bytes()).hexdigest()==stock['sha256']
cache=root/'build/environment-cache'/stock['sha256']/stock['name'];assert cache.is_file()
# Only move our generated fixture and owned cache inside BTS_ROOT, with unconditional restoration.
for path in [a.source,cache]:assert path.resolve().is_relative_to(root.resolve())
backups=[];children=set()
try:
    for path in [a.source,cache]:
        hidden=path.with_name(path.name+'.proof-hidden');assert not hidden.exists();path.rename(hidden);backups.append((path,hidden))
    command=['bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--open-document',str(a.output/'stock/stock.blend'),
        '--reject-document',str(a.output/'stock/bad-hash.blend'),'--open-demo-dir',str(a.output/'host'),'--environment','/nonexistent-environment.hdr']
    with (a.output/'host.log').open('w') as log:
        process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env={**os.environ,'BTS_ROOT':str(root),'BTS_ENVIRONMENT':'/nonexistent-environment.hdr'})
        started=time.monotonic()
        while process.poll() is None:
            try:
                if Path(f'/proc/{process.pid}/exe').resolve(strict=True).name=='bts-host':
                    for task in Path(f'/proc/{process.pid}/task').glob('*/children'):children.update(task.read_text().split())
            except FileNotFoundError:pass
            if time.monotonic()-started>120:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
                raise AssertionError('Environment Open timed out')
            time.sleep(.1)
    assert process.returncode==0 and not children,(process.returncode,children)
    assert cache.is_file() and hashlib.sha256(cache.read_bytes()).hexdigest()==stock['sha256']
finally:
    for path,hidden in reversed(backups):hidden.replace(path)
log=(a.output/'host.log').read_text();assert 'BTS_SHUTDOWN clean' in log and 'BTS_INVALID_PROJECT_PRESERVED' in log and 'Not freed memory blocks' not in log
opened=json.loads((a.output/'host/open.json').read_text())['captures'][0]
assert opened['profile']['environment']==stock['name'] and not opened['undo_enabled']
original=json.loads((a.demo/'resource.json').read_text())['captures'][3]['document'];assert opened['document']==original
result={'pid':process.pid,'children':[],'clean_exit':True,'stock':stock,'source_and_cache_absent_during_open':True,
    'packed_bytes_restored_exactly':True,'bad_environment_hash_preserves_live_document':True,'authored_document_unchanged':True}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
