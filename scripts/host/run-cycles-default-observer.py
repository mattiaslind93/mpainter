"""Strict runtime-only boundary for the single default-observation-only-v2 cohort.

Does not build, rewrite the frozen builder/manifests, or configure sampler RNA.
"""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import resource
import subprocess
import time

import cycles_integrated_snapshot as integration
from cycles_live_manifest import fresh_runtime, freeze_admission

PROFILE = 'default-observation-only-v2'
WRITER_SHA = '76fda7d12abfd5d6ad80fd469c7a59af1c5d89641f53d2284c5cba8e7309faff'
STAGES = ['cycles-before', 'cycles-color-edited', 'cycles-color-undone', 'cycles-color-redone',
          'cycles-camera-only', 'cycles-reentered-latest']
sha = integration.sha256

def require(okay, message):
    if not okay:
        raise ValueError(message)

def read(path):
    return json.loads(Path(path).read_text())

def writer_module(path):
    require(sha(path) == WRITER_SHA, 'Changed frozen build/admit writer')
    spec = importlib.util.spec_from_file_location('frozen_default_observer_writer', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def reused_tests(writer, options, parent, donor):
    """Audit ALL retained donor tests; report unused cacheCPU separately."""
    old_actor = writer.unit(donor, writer.PAIRS['actor'][0])['object']
    allowed = writer.headers(options.snapshot, options.baseline, donor, parent,
                             options.snapshot / 'integration-source')
    rows = {}
    for unit in donor['compiled_test_units']:
        if unit['object'] == old_actor:
            continue
        obj = Path(unit['object'])
        require(sha(obj) == unit['object_sha256'], 'Changed retained donor test object: ' + str(obj))
        command = unit['command']; source = Path(command[command.index('-c') + 1])
        require(sha(source) == unit['source_sha256'], 'Changed retained donor test source')
        scan = donor['frozen_preprocessor_closure'][Path(unit['source']).name]
        dependency = Path(scan['command'][scan['command'].index('-MF') + 1])
        require(sha(dependency) == scan['dependency_sha256'], 'Changed retained donor discovery receipt')
        discovered = writer.closure(dependency, options.snapshot, allowed)
        compiled = writer.closure(Path(command[command.index('-MF') + 1]), options.snapshot, allowed)
        require({str(Path(p).resolve()) for p in discovered} == {str(Path(p).resolve()) for p in compiled},
                'Retained donor compiled/declaration dependency sets differ')
        rows[str(obj.resolve())] = {'sha256': unit['object_sha256'], 'source': writer.record(source),
                                  'dependencies': compiled, 'linked': False}
    return rows

def link_controls(command, parent, snapshot, retained, new_objects):
    """Classify every actual link input, including every repeated archive token."""
    expected = {}
    def add(path, digest, kind):
        key = str(Path(path).resolve())
        require(key not in expected or expected[key]['sha256'] == digest, 'Conflicting input ownership')
        expected[key] = {'sha256': digest, 'kind': kind}
    for info in parent['link_inputs'].values():
        path = snapshot / info['path'] if info['kind'] == 'frozen' else Path(info['path'])
        add(path, info['sha256'], 'production-' + info['kind'])
    for path, row in retained.items(): add(path, row['sha256'], 'retained-donor-test')
    for row in new_objects.values(): add(row['path'], row['sha256'], 'new-observer-test')
    actual = {}
    for index, token in enumerate(command):
        if not integration.is_link_input(token):
            continue
        path = Path(token); path = path if path.is_absolute() else snapshot / path
        key = str(path.resolve())
        require(key in expected, 'Unknown actual link input: ' + token)
        require(sha(path) == expected[key]['sha256'], 'Changed actual link input: ' + token)
        row = actual.setdefault(key, {**expected[key], 'token_indices': []})
        row['token_indices'].append(index)
        if key in retained: retained[key]['linked'] = True
    for row in new_objects.values():
        require(str(Path(row['path']).resolve()) in actual, 'Missing new observer object in link')
    require(actual, 'Empty actual link input control map')
    return actual

def audit(options, repo, writer):
    writer.admit(options, repo, options.output)  # Frozen76fd, unchanged full BASE and strict805→806.
    parent = read(options.snapshot / 'integrated-snapshot.json')
    donor = read(options.baseline / 'build.json'); built = read(options.output / 'build.json')
    retained = reused_tests(writer, options, parent, donor)
    inputs = link_controls(built['link']['command'], parent, options.snapshot, retained,
                           {label: row['object'] for label, row in built['compiled_test_units'].items()})
    sources = {str(path.resolve()): sha(path) for path in (
        Path(__file__), Path(writer.__file__), repo / 'scripts/host/cycles_integrated_snapshot.py',
        repo / 'scripts/host/cycles_live_manifest.py', repo / 'scripts/host/run-host.sh',
        repo / 'scripts/host/gpu-env.sh', repo / 'scripts/host/session_logging.py',
        repo / 'fixtures/two_tile_udim.obj', options.environment)}
    return {'profile': PROFILE, 'retained_donor_tests': retained, 'actual_link_inputs': inputs,
            'runtime_sources': sources, 'build': writer.record(options.output / 'build.json'),
            'run_manifest': writer.record(options.output / 'run-manifest.json')}

def terminal_events(trace):
    require(not trace.get('failed') and not trace.get('failure'), 'Observer callback/restoration failure')
    sessions = trace.get('sessions', [])
    require(sessions and all(s.get('viewport') is True and s.get('background') is False and s.get('auto_tile') is False for s in sessions),
            'Missing single-tile viewport Session proof')
    generations = [s['generation'] for s in sessions]
    require(len(set(generations)) == len(generations), 'Repeated Session identity')
    states, terminal = {}, {}
    for event in trace.get('events', []):
        generation, sample, progress = event['generation'], event['sample'], event['progress']
        require(generation in generations and type(sample) is int and sample >= 0 and type(progress) in (int, float) and math.isfinite(progress) and 0 <= progress <= 1,
                'Invalid actual progress observation')
        state = states.setdefault(generation, {'previous': None, 'epoch': 1, 'active': False, 'sequence': 0})
        require(event['sequence'] > state['sequence'], 'Nonmonotonic callback sequence')
        done = 'Rendering Done' in event['status']
        dropped = state['previous'] is not None and sample < state['previous']
        if dropped:
            state['epoch'] += 1; state['active'] = False
        elif progress < 1 and not done:
            state['active'] = True
        require(event['epoch'] == state['epoch'], 'Epoch is not tied to actual sample drop')
        if not dropped and state['active'] and sample > 0 and progress == 1 and done:
            terminal[(generation, event['epoch'], event['sequence'])] = event
        state['previous'], state['sequence'] = sample, event['sequence']
    return terminal

def clean_markers(log, pid):
    return re.findall(r'\bBTS_SHUTDOWN clean pid=' + re.escape(str(pid)) + r'(?=\s|$)', log)

def private_environment(inherited, runtime, bts, snapshot, binary, environment):
    env = {key: value for key, value in inherited.items()
           if not key.startswith(('BTS_', 'XDG_', 'QT_')) and key != 'WAYLAND_DISPLAY'}
    for key, folder in (('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_CACHE_HOME', 'cache'),
                        ('XDG_STATE_HOME', 'state'), ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'),
                        ('BTS_SESSION_LOG_DIR', 'logs'), ('TMPDIR', 'tmp'), ('XDG_RUNTIME_DIR', 'runtime')):
        env[key] = str(runtime / folder)
    env.update(BTS_ROOT=str(bts), CUDA_CACHE_PATH=str(runtime / 'cache/cuda'), __GL_SHADER_DISK_CACHE_PATH=str(runtime / 'cache/nvidia'),
               BTS_VERIFY_CYCLES_LIVE='1', BTS_BA_EXPECT_FIXED='1', QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
               BTS_BLEND_MATERIAL_DEMO='1', BTS_HOST_EXECUTABLE=str(binary), QT_PLUGIN_PATH=str(bts / 'qt/6.8.3/plugins'),
               LD_LIBRARY_PATH=str(bts / 'qt/6.8.3/lib') + ':' + str(snapshot / 'bin/lib'), BTS_ENVIRONMENT=str(environment),
               LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8')
    return env

def complete_attempt(runtime, pid, returncode):
    log = (runtime / 'run.log').read_text()
    require(returncode == 0, 'Native OS return code is not zero: ' + str(returncode))
    require(len(clean_markers(log, pid)) == 1, 'Expected one bounded clean marker for actual native PID')
    require(re.search(r'BTS_SCENE_READY pid=' + str(pid) + r'\b[^\n]*native_window=none', log), 'Missing actual unmapped native Session')
    require(re.search(r'BTS_GPU_READY pid=' + str(pid) + r'\b[^\n]*renderer=NVIDIA', log) and
            re.search(r'BTS_CYCLES_DEVICE GPU \[[^\n]*NVIDIA', log), 'Missing positive hardware GPU evidence')
    require(log.count('BTS_BA_DEFAULT_OBSERVED_V2_DONE') == 1, 'Missing unique v2 final marker')
    report = read(runtime / 'cycles-udim.json'); trace = read(runtime / 'default-completion-trace.json')
    require(report.get('profile') == PROFILE and [r.get('stage') for r in report.get('captures', [])] == STAGES,
            'Wrong profile/incomplete original six stages')
    terminals = terminal_events(trace); epochs = []
    for capture in report['captures']:
        c = capture['actual_completion']; generation, epoch, seq = int(c['generation']), int(c['epoch']), int(c['sequence'])
        event = terminals.get((generation, epoch, seq))
        require(event and c['sample'] == event['sample'] and c['progress'] == 1 and 'Rendering Done' in c['status'] and
                int(c['terminal_epoch']) == epoch and int(c['draw_generation']) == generation and int(c['draw_epoch']) == epoch,
                'Capture lacks observed active→terminal epoch and matching draw')
        first_terminal = min(row['steady_ns'] for key, row in terminals.items() if key[:2] == (generation, epoch))
        require(first_terminal < int(c['paint_begin_ns']) <= int(c['paint_done_ns']), 'Paint did not follow actual terminal callback')
        epochs.append((generation, epoch))
    require(all(epochs[i][0] == epochs[0][0] and epochs[i][1] > epochs[i - 1][1] for i in range(1, 5)) and
            epochs[5][0] > epochs[0][0], 'Missing distinct reset/reentry generations')
    baseline = report['captures'][0]['observed_default_settings']
    require(all(row['observed_default_settings'] == baseline for row in report['captures'][:4]), 'Observed default settings changed during edit/Undo/Redo')
    sampler = ('samples', 'sampling_pattern', 'filter_type', 'filter_width', 'adaptive', 'animated_seed', 'seed', 'pixel_jitter', 'denoise', 'frame', 'color_management')
    require(all(all(row['observed_default_settings'].get(key) == baseline.get(key) for key in sampler) for row in report['captures']), 'Observed sampler/color settings changed')
    # Original strict ROI equality, with no tolerance or conversion changes.
    for before, after in [('cycles-before', 'cycles-color-undone'), ('cycles-color-edited', 'cycles-color-redone')]:
        require(sha(runtime / (before + '-mesh-roi.png')) == sha(runtime / (after + '-mesh-roi.png')), 'Strict ROI restoration differs')
    for phase in ('IDLE', 'CAMERA'):
        begin, end = 'BTS_BA_' + phase + '_BEGIN', 'BTS_BA_' + phase + '_END'
        require(log.count(begin) == log.count(end) == 1, 'Missing unique idle/camera interval')
        interval = log.split(begin, 1)[1].split(end, 1)[0]
        require(not any(marker in interval for marker in ('BTS_CYCLES_MIRROR_', 'BTS_CYCLES_RELOAD', 'BTS_CYCLES_IMAGE_RELOAD')),
                'Unexpected idle/camera mirror traffic')
    return {'profile': PROFILE, 'stages': STAGES, 'terminal_epochs': epochs, 'native_pid': pid, 'native_returncode': returncode}

def execute(options, repo, writer):
    before = audit(options, repo, writer)
    runtime = fresh_runtime(options.output)
    (runtime / 'pre-control.json').write_text(json.dumps(before, indent=2) + '\n')
    manifest = options.output / 'run-manifest.json'
    binary = freeze_admission(manifest, options.output / 'bin/cycles-live-artist', runtime)
    bts = repo.parent
    env = private_environment(os.environ, runtime, bts, options.snapshot, binary, options.environment)
    for folder in ('config', 'data', 'cache', 'state', 'recovery', 'resources', 'logs', 'tmp', 'runtime'):
        (runtime / folder).mkdir(mode=0o700 if folder == 'runtime' else 0o755)
    (runtime / 'runtime').chmod(0o700)
    for folder in ('cuda', 'nvidia'): (runtime / 'cache' / folder).mkdir()
    command = ['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'), '--workspace', '--no-splash',
               '--gui-fix-demo', str(runtime), '--gui-fix-mesh', str(repo / 'fixtures/two_tile_udim.obj'),
               '--gui-fix-resolution', '2048', '--library-dir', str(runtime / 'shelf'), '--recovery-dir', str(runtime / 'recovery')]
    result = {'profile': PROFILE, 'command': command, 'runtime': str(runtime), 'returncode': 1}
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    try:
        with (runtime / 'run.log').open('w') as log:
            process = subprocess.Popen(command, cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT)
            result['native_pid'] = process.pid
            (runtime / 'process.json').write_text(json.dumps(result, indent=2) + '\n')
            print('Native observer started pid=' + str(process.pid) + ' runtime=' + str(runtime), flush=True)
            try: result['native_returncode'] = process.wait(timeout=420)
            except subprocess.TimeoutExpired:
                process.terminate()
                try: process.wait(timeout=20)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
                result['native_returncode'] = process.returncode
                raise RuntimeError('Bounded native timeout; terminated, no retry')
        # The existing timestamp tee may finish just after the actual native wait.
        for unused in range(20):
            if clean_markers((runtime / 'run.log').read_text(), process.pid): break
            time.sleep(0.05)
        result['verified'] = complete_attempt(runtime, process.pid, result['native_returncode'])
        result['returncode'] = 0
    except Exception as error:
        result['error'] = str(error)
    finally:
        try:
            after = audit(options, repo, writer)
            (runtime / 'post-control.json').write_text(json.dumps(after, indent=2) + '\n')
            require(after == before, 'Runtime changed retained/actual link inputs, source or dependencies')
            result['input_controls_equal'] = True
        except Exception as error:
            result['returncode'] = 1; result['post_guard_error'] = str(error)
        (runtime / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2), flush=True)
    return result['returncode']

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=[PROFILE], required=True)
    for name in ('snapshot', 'baseline', 'probe-donor', 'syntax-receipt', 'output', 'environment'):
        parser.add_argument('--' + name, type=Path, required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--admit-only', action='store_true'); modes.add_argument('--run-only', action='store_true')
    options = parser.parse_args()
    for name in ('snapshot', 'baseline', 'probe_donor', 'syntax_receipt', 'output', 'environment'):
        setattr(options, name, Path(os.path.abspath(getattr(options, name))))
    repo = Path(__file__).resolve().parents[2]
    writer = writer_module(repo / 'scripts/host/verify-cycles-default-observer.py')
    if options.admit_only:
        control = audit(options, repo, writer)
        target = options.output / 'runtime-admission.json'
        require(not target.exists(), 'Do not overwrite runtime admission')
        target.write_text(json.dumps(control, indent=2) + '\n'); print('Admission only: ' + str(target))
        return 0
    return execute(options, repo, writer)

if __name__ == '__main__':
    raise SystemExit(main())
