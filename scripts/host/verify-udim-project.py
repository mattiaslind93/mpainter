"""Packed UDIM -> stock Blender -> fresh native host, with no external HDRI."""
import argparse,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--paint-demo',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];root=repo.parent;a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'stock.log').open('w') as log:
    subprocess.run(['bash','-c','source "$1"; export WAYLAND_DISPLAY=; exec "$2" --background --factory-startup --disable-autoexec "$3" --python "$4" -- "$5"',
        'stock-udim',str(repo/'scripts/host/gpu-env.sh'),str(root/'build/baseline/bin/blender'),str(a.paint_demo/'painted.blend'),
        str(repo/'scripts/host/stock-udim-roundtrip.py'),str(a.output/'stock')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
stock=json.loads((a.output/'stock/stock.json').read_text())
command=['bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--open-document',str(a.output/'stock/stock.blend'),
    '--open-demo-dir',str(a.output/'host'),'--environment','/nonexistent-external-environment.hdr']
children=set()
with (a.output/'host.log').open('w') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env={**os.environ,'BTS_ROOT':str(root),'BTS_ENVIRONMENT':'/nonexistent-external-environment.hdr'})
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
            raise AssertionError('UDIM Open timed out')
        time.sleep(.1)
assert process.returncode==0 and not children,(process.returncode,children)
log=(a.output/'host.log').read_text();assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
opened,edited=json.loads((a.output/'host/open.json').read_text())['captures']
assert opened['document']==stock['metadata']
assert not opened['undo_enabled'] and not opened['redo_enabled'] and edited['undo_enabled']
a_layer=opened['document']['texture_sets'][0]['layers'][0];b_layer=edited['document']['texture_sets'][0]['layers'][0]
assert a_layer['id']==b_layer['id']
for before,after in zip(a_layer['paint_tiles'],b_layer['paint_tiles']):
    assert before['number']==after['number']
    assert all(x!=y for x,y in zip(before['hashes'],after['hashes']))
    assert all(n>0 for n in after['pages'])
result={'pid':process.pid,'children':[],'clean_exit':True,'stock_version':stock['version'],'stock_hash':stock['build_hash'],
    'native_tiled_images':stock['images'],'packed_author_images':stock['packed_author_images'],
    'exact_metadata_after_stock_save':True,'same_layer_edited_all_channels_both_tiles':True,'environment_restored_from_packed_bytes':True}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
