"""Verify close latency, helper semantics and persistence using a private Qt stage."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--qt-stage', required=True, type=Path)
parser.add_argument('--qt-prefix', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--repeats', type=int, default=3)
args = parser.parse_args()
if args.repeats < 1:
    parser.error('--repeats must be positive')
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', LC_ALL='C.UTF-8',
           QT_PLUGIN_PATH=str(args.qt_prefix / 'plugins'),
           LD_LIBRARY_PATH=f'{args.qt_stage / "lib"}:{args.qt_prefix / "lib"}',
           XDG_CONFIG_HOME=str(output / 'config'), XDG_DATA_HOME=str(output / 'data'),
           XDG_CACHE_HOME=str(output / 'cache'), BTS_RECOVERY_DIR=str(output / 'recovery'),
           TMPDIR=str(output / 'tmp'))
for name in ('config', 'data', 'cache', 'recovery', 'tmp'):
    (output / name).mkdir(exist_ok=True)

def run(mode, root, log, *, stock=False, trace=None):
    root.mkdir(parents=True, exist_ok=True)
    application_env = dict(env)
    if stock:
        application_env['LD_LIBRARY_PATH'] = str(args.qt_prefix / 'lib')
    command = ['nice', '-n', '19']
    if trace:
        command += ['strace', '-f', '-tt', '-T', '-s', '256', '-e',
                    'trace=write,writev,openat,close,fsync,fdatasync', '-o', str(trace)]
    command += ['stdbuf', '-oL', '-eL', str(args.executable.resolve()), mode, str(root)]
    with log.open('w') as stream:
        subprocess.run(command, env=application_env, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=120)
    return log.read_text()

close_times = []
for index in range(args.repeats):
    root = output / f'roundtrip-{index}'
    text = run('--write', root, output / f'close-{index}.log')
    elapsed = float(re.search(r'accept\+destroy ([0-9.]+) ms', text)[1])
    assert elapsed < 50, elapsed
    close_times.append(elapsed)
    run('--read', root, output / f'read-{index}.log')
state_root = output / 'state-roundtrip'
held_write = run('--state-write', state_root, output / 'state-write.log')
run('--state-read', state_root, output / 'state-read.log')
plain = run('--plain-qt', output / 'plain', output / 'plain.log')
assert 'keeps upstream settings persistence' in plain
stock = run('--stock-qt', output / 'stock', output / 'stock.log', stock=True)
assert 'BTS_FILE_DIALOG_QT_PATCH_MISSING' in stock
trace = output / 'close.strace'
run('--write', output / 'traced', output / 'traced.log', trace=trace)
lines = trace.read_text().splitlines()
begin = next(i for i, line in enumerate(lines) if 'BTS_FILE_DIALOG_TIMED_CLOSE_BEGIN' in line)
end = next(i for i, line in enumerate(lines) if 'BTS_FILE_DIALOG_TIMED_CLOSE_END' in line)
main_pid = lines[begin].split()[0]
assert begin < end
ui_sync = [line for line in lines[begin:] if line.split()[0] == main_pid and re.search(r'\b(?:fsync|fdatasync)\(', line)]
worker_sync = [line for line in lines[begin:] if line.split()[0] != main_pid and re.search(r'\b(?:fsync|fdatasync)\(', line)]
assert not ui_sync, ui_sync
assert worker_sync, 'expected durable state/history writes on a worker'
result = {'close_limit_ms': 50, 'close_ms': close_times,
          'actual_file_acceptance': True, 'recent_pinned_process_restart': True,
          'state_process_restart_and_shutdown_drain': True,
          'held_state_write': held_write.splitlines(), 'static_helpers': True,
          'plain_qt_persistence_unchanged': True, 'stock_qt_warning_and_fallback': True,
          'ui_fsync_after_close_begin': len(ui_sync), 'worker_fsync_after_close_begin': len(worker_sync),
          'trace': str(trace)}
(output / 'verified.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
