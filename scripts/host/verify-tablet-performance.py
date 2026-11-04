"""Queued native tablet/mouse input versus the identical unbatched polyline."""
from parity_test_environment import private_display
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
env = dict(os.environ, LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8',
           DISPLAY=private_display(), XDG_RUNTIME_DIR='/run/user/1000', BTS_TABLET_PERFORMANCE='1')
command = ['bash', str(root/'scripts/host/run-host.sh'), '--workspace',
           '--gui-fix-demo', str(a.output), '--gui-fix-mesh', str(root/'fixtures/single_tile.obj'),
           '--gui-fix-resolution', '1024', '--recovery-dir', str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        code = process.wait(timeout=300)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=15)
        raise
assert code == 0, code
demo = json.loads((a.output/'tablet-performance.json').read_text())
assert demo['pid'] == process.pid and demo['queued_events'] and demo['resolution'] == 1024
assert len(demo['runs']) == 4
assert demo['hover']['after'] - demo['hover']['before'] == 1, demo['hover']
results = []
for burst, oracle in zip(demo['runs'][::2], demo['runs'][1::2]):
    assert not burst['oracle'] and oracle['oracle'] and burst['moves'] >= 400
    assert burst['max_ping_ms'] < 100, burst
    assert burst['hash'] != burst['before']['hash'] and burst['hash'] == oracle['hash'], (burst, oracle)
    assert burst['bump_hash'] == oracle['bump_hash'], (burst, oracle)
    for run in [burst, oracle]:
        assert run['undo_count'] == run['before']['undo_count'] + 1, run
        assert run['decimal'] == '.' and not run['shader_failures'], run
        assert run['input_points']-run['before']['input_points']==402,run['input_points']-run['before']['input_points']
    batches = burst['stroke_batches'] - burst['before']['stroke_batches']
    frames = burst['input_frames'] - burst['before']['input_frames']
    presented = burst['render_frames'] - burst['before']['render_frames']
    assert 0 < batches <= frames + 2 and batches <= presented + 2, (batches, frames, presented)
    if burst['input'] == 'tablet':
        assert burst['last_dab_diameter'] > burst['first_dab_diameter'] * 3, burst
    results.append(dict(input=burst['input'], moves=burst['moves'], max_ping_ms=burst['max_ping_ms'],
                        batches=batches, input_frames=frames, presented_frames=presented,
                        pixel_hash_matches_unbatched=True, single_undo=True,received_input_points=burst["input_points"]-burst["before"]["input_points"],
                        first_diameter=burst['first_dab_diameter'], last_diameter=burst['last_dab_diameter']))
log = (a.output/'run.log').read_text()
assert not re.search(r'Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED', log, re.I), log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result = dict(ignored_native_input=demo.get('ignored_native_input',0), pid=process.pid, locale='sv_SE.UTF-8', resolution=1024, queued_events=True, hover=demo['hover'], runs=results, clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
