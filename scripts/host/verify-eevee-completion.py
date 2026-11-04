"""Observe two fresh EEVEE sampling terminals through normal product redraws.

This verifies copied postdraw state and a later Paint, never a GPU fence,
presentation, evaluated graph epoch, final surface or latency bound.
Run inside bts-build with nice19 and a separately qualified matching host.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import uuid


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def uint(value):
    """Actor serializes uint64 counters as canonical decimal strings."""
    if type(value) is not str or re.fullmatch(r'0|[1-9][0-9]*', value) is None:
        raise ValueError('Expected unsigned decimal counter')
    number = int(value)
    if number > 2**64 - 1:
        raise ValueError('Counter outside native uint64 domain')
    return number


def integer(value, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError('Expected integer, not Boolean/coercion')
    return value


def identity(value):
    if type(value) is not str or uuid.UUID(value).int == 0:
        raise ValueError('Expected nonzero native UUID')
    return value


def eevee_route(route):
    return (type(route) is dict and route.get('active') is False
            and route.get('device') == 'EEVEE' and route.get('status') == 'EEVEE')


def valid_terminal(row, phase):
    """Independently check the actor's copied values and actual Paint identity."""
    try:
        if type(row) is not dict or row['status'] != 'Captured':
            return False
        if any(row[key] is not True for key in [
                'fresh_current_scene_draw_and_later_Paint',
                'terminal_sampling_state_observed', 'target_ready',
                'viewport_sampling_finished', 'needed_shaders_loaded', 'render_extent_valid']):
            return False
        if row['interactive'] is not False or row['reset_pending'] is not False:
            return False
        if integer(row['phase_at_Paint_entry']) != phase or integer(row['current_phase']) != phase:
            return False
        if integer(row['frames_after_Paint']) <= integer(row['frames_before_Paint']):
            return False
        state = row['state']
        if integer(state['render_frames']) != row['frames_after_Paint']:
            return False
        revision = integer(row['revision_before_Paint'])
        if integer(row['revision_after_Paint']) != revision or integer(state['document_revision']) != revision:
            return False
        if any(state[key] is not False for key in [
                'open_pending', 'composite_pending', 'property_pending', 'texture_loading']):
            return False
        draw = uint(row['host_draw'])
        if draw <= max(uint(row['draw_before_Paint']), uint(row['phase_draw_floor'])):
            return False
        if uint(row['paint_sequence']) == 0 or uint(row['scene_target_generation']) == 0:
            return False
        if integer(row['width'], 1) == 0 or integer(row['height'], 1) == 0:
            return False
        if integer(row['active_view']) not in (0, 1) or uint(row['sample_count']) == 0:
            return False
        uint(row['sample_index'])
        if integer(row['queued_shaders']) != 0 or integer(row['queued_textures']) != 0:
            return False
        if not eevee_route(row['host_backend_route']):
            return False
        identity(row['document_id']); identity(row['set_id'])
        integer(row['selected_material_uid'], 1)
        integer(state['undo_count'])
        return True
    except (KeyError, TypeError, ValueError, AttributeError):
        return False


def valid_observation(proof, native_pid):
    """Two new phase-specific terminals; a cached snapshot cannot satisfy this."""
    try:
        if type(proof) is not dict or proof['profile'] != 'eevee-completion-observation-only-v1':
            return False
        if proof['passed'] is not True or type(proof['native_pid']) is not int or proof['native_pid'] != native_pid:
            return False
        if proof['phase'] != 2 or proof['stage'] != 3 or proof['error'] != '':
            return False
        if any(proof[key] is not True for key in [
                'natural_product_redraw_only', 'snapshot_not_a_completion_epoch']):
            return False
        if any(proof[key] is not False for key in [
                'settings_written', 'final_surface_acceptance', 'shader_math_changed']):
            return False
        rows, terminals = proof['observations'], proof['terminal_sampling_observations']
        if type(rows) is not list or not rows or type(terminals) is not list or len(terminals) != 2:
            return False
        first, second = terminals
        if not valid_terminal(first, 1) or not valid_terminal(second, 2):
            return False
        # The actual terminal rows must exist in the retained post-Paint stream.
        if sum(row == first for row in rows) != 1 or sum(row == second for row in rows) != 1:
            return False
        if rows.index(first) >= rows.index(second):
            return False
        if uint(second['paint_sequence']) <= uint(first['paint_sequence']):
            return False
        if uint(second['host_draw']) <= uint(first['host_draw']):
            return False
        if uint(second['phase_draw_floor']) < uint(first['host_draw']):
            return False
        for key in ['document_id', 'set_id', 'selected_material_uid']:
            if first[key] != second[key]:
                return False
        if second['revision_after_Paint'] <= first['revision_after_Paint']:
            return False
        if second['state']['undo_count'] != first['state']['undo_count'] + 1:
            return False
        if proof['state']['document_revision'] != second['revision_after_Paint']:
            return False
        if not eevee_route(proof['last_backend_route']):
            return False
        # Later ordinary draws may have happened; never require a stale exact draw.
        last = proof['last_snapshot']
        if last['status'] != 'Captured' or uint(last['host_draw']) < uint(second['host_draw']):
            return False
        return True
    except (KeyError, TypeError, ValueError, AttributeError):
        return False


def descendants(pid):
    result = [pid]
    try:
        children = (Path('/proc') / str(pid) / 'task' / str(pid) / 'children').read_text().split()
    except FileNotFoundError:
        return result
    for child in children:
        result.extend(descendants(int(child)))
    return result


def communicate_owned(child, timeout):
    """Wait for actual pipe EOF; timeout retires only this run's descendants."""
    try:
        raw, _ = child.communicate(timeout=timeout)
        return raw, False
    except subprocess.TimeoutExpired:
        groups = set()
        for pid in descendants(child.pid):
            try:
                groups.add(os.getpgid(pid))
            except ProcessLookupError:
                pass
        require(os.getpgrp() not in groups, 'Refuse to terminate verifier owner group')
        for group in groups:
            try:
                os.killpg(group, signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            raw, _ = child.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            for group in groups:
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            raw, _ = child.communicate()
        return raw, True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--host-sha256', required=True, help='Separately qualified matching candidate hash')
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2],
                        help='Matching native actor and launcher source tree')
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(os.environ.get('CONTAINER_ID') or Path('/run/.containerenv').exists(), 'Enter bts-build first')
    require(os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run nice19 inside bts-build')
    require(re.fullmatch(r'[0-9a-f]{64}', args.host_sha256) is not None, 'Exact qualified binary hash required')
    source = args.source_root.resolve()
    expected_build = (Path(os.environ.get('BTS_ROOT', '/home/mlind/bts')) / 'build').resolve()
    out = args.output.resolve()
    build = next((p for p in out.parents if p.exists() and p.samefile(expected_build)), None)
    require(build is not None and not out.exists(), 'Fresh private directory under actual build required')
    out.mkdir(parents=True)
    started = time.monotonic()
    result = {'actual_exit': 1, 'scope': 'Postdraw sampling/queue plus later Paint observation only; no GPU fence, presentation, graph epoch, final-surface or latency claim'}
    controls = {}
    def write(path, value): path.write_text(json.dumps(value, indent=2) + '\n')
    def guard():
        for path, expected in controls.items():
            require(Path(path).is_file() and sha(path) == expected, 'Input drift: ' + path)
    try:
        host, environment = args.host.resolve(), args.environment.resolve()
        require(sha(host) == args.host_sha256, 'Qualified binary mismatch')
        files = [host, environment, Path(__file__).resolve()]
        for rel in ['scripts/host/run-host.sh', 'scripts/host/gpu-env.sh', 'scripts/host/session_logging.py',
                    'native/tests/pd149_uniform_fixture.py', 'native/tests/MaterialLayerEeveeObservation.hh',
                    'native/tests/MaterialLayerEeveeObservationArtist.inc', 'native/host/EeveeCompletionSnapshot.hh',
                    'native/host/EeveeCompletionSnapshot.cc', 'native/tests/material_layering_ui_artist_test.cc']:
            files.append(source / rel)
        controls.update({str(p): sha(p) for p in files})
        fixture_path = source / 'native/tests/pd149_uniform_fixture.py'
        spec = importlib.util.spec_from_file_location('eevee_completion_fixture', fixture_path)
        fixture = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixture)
        mesh = fixture.write_fixture(out / 'fixture', build)
        controls.update({str(p): sha(p) for p in [mesh, mesh.with_suffix('.mtl')]})
        output = out / 'output'; output.mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_USER_'))}
        require(env.get('DISPLAY'), 'Inherit proven offscreen GLX DISPLAY/XAUTHORITY')
        env.pop('WAYLAND_DISPLAY', None)
        env.update(BTS_ROOT=str(build.parent), BTS_HOST_EXECUTABLE=str(host), BTS_ENVIRONMENT=str(environment),
                   BTS_HEADLESS_WORKSPACE='1', BTS_PD149_EEVEE_OBSERVATION='1',
                   QT_QPA_PLATFORM='offscreen', LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8',
                   PYTHONDONTWRITEBYTECODE='1', LD_LIBRARY_PATH=str(host.parent / 'lib'))
        for key, name in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),
                          ('XDG_STATE_HOME','state'),('XDG_RUNTIME_DIR','runtime'),('TMPDIR','tmp'),
                          ('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs'),
                          ('BTS_ASSET_LIBRARY_DIR','library'),('BTS_TEMPLATE_DIR','templates'),('BTS_USER_CONFIG_DIR','user-config'),
                          ('BLENDER_USER_CONFIG','blender-config'),('BLENDER_USER_DATAFILES','blender-data'),
                          ('BLENDER_USER_SCRIPTS','blender-scripts'),('CUDA_CACHE_PATH','cuda'),('__GL_SHADER_DISK_CACHE_PATH','gl-cache')]:
            directory = out / name; directory.mkdir(mode=0o700); env[key] = str(directory)
        command = ['bash', str(source / 'scripts/host/run-host.sh'), '--no-splash', '--workspace',
                   '--material-layering-demo', str(output), '--gui-fix-mesh', str(mesh),
                   '--gui-fix-resolution', '128', '--autosave-seconds', '3600', '--library-dir', env['BTS_ASSET_LIBRARY_DIR']]
        write(out / 'admission.json', {'command': command, 'source_root': str(source), 'controls': controls,
              'actor_env': 'BTS_PD149_EEVEE_OBSERVATION', 'resolution': 128, 'fixture_tiles': [1001, 1002],
              'xauthority_present': bool(env.get('XAUTHORITY')),
              'xauthority_value_sha256': hashlib.sha256(env['XAUTHORITY'].encode()).hexdigest() if env.get('XAUTHORITY') else None,
              'timeout_seconds': 240, 'scope': result['scope']})
        guard()
        child = subprocess.Popen(command, cwd=out, env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        result.update(native_pid=child.pid, binary_sha256=args.host_sha256)
        write(out / 'running.json', result)
        raw, timed_out = communicate_owned(child, 240)
        (out / 'run.log').write_bytes(raw)
        text = raw.decode(errors='replace')
        renderer = re.findall(r'BTS_GPU_READY pid=' + str(child.pid) + r' renderer=(.*?) qt=', text)
        clean = re.findall(r'\bBTS_SHUTDOWN clean pid=(\d+)\b', text)
        scene = re.findall(r'BTS_SCENE_READY pid=' + str(child.pid) + r' .*?native_window=none\b', text)
        forbidden = re.findall(r'BTS_DRAW_FAILED|BTS_EXIT_FAILED|Shader compile failed|Segmentation fault|AddressSanitizer|LeakSanitizer|BTS_GPU_STACK_POLL_FAILED|BTS_GPU_STACK_PROGRAM_FAILED|BTS_SHARED_CONTEXT_FAILED|BTS_SHADER_SHARED_CONTEXT_FAILED|Cannot make QOpenGLContext current|QThread: Destroyed|ID.user.(?:count|decrement).*error|users on ID', text, re.I)
        result.update(native_exit=child.returncode, timed_out=timed_out, clean_pid_matches=clean == [str(child.pid)],
                      renderer=renderer, unmapped_scene_ready=len(scene) == 1, forbidden=forbidden,
                      run_log_sha256=sha(out / 'run.log'))
        proof_path = output / 'pd149-eevee-observation.json'
        if proof_path.is_file():
            proof = json.loads(proof_path.read_text())
            result.update(proof_sha256=sha(proof_path), terminal_predicate=valid_observation(proof, child.pid),
                          actor_passed=proof.get('passed'), actor_error=proof.get('error'), actor_stage=proof.get('stage'), actor_phase=proof.get('phase'))
        else:
            result['terminal_predicate'] = False
        require(not timed_out and child.returncode == 0 and result['clean_pid_matches'], 'Native timeout/OS/CLEAN negative retained')
        require(len(renderer) == 1 and 'NVIDIA' in renderer[0] and 'RTX' in renderer[0]
                and not re.search(r'llvmpipe|lavapipe|software', renderer[0], re.I), 'Offscreen RTX renderer required')
        require(result['unmapped_scene_ready'] and not forbidden, 'Unmapped/forbidden-log negative')
        require(text.count('PD149_EEVEE_OBSERVATION PASS') == 1 and result['terminal_predicate'],
                'Two fresh phase1/2 postPaint sampling terminals not proven; full JSON/log retained')
        guard(); result['actual_exit'] = 0
    except BaseException as error:
        result['error'] = repr(error)
    finally:
        try: guard(); result['postguards_passed'] = True
        except BaseException as error:
            result.update(actual_exit=1, postguards_passed=False, postguard_error=repr(error))
        result['seconds'] = time.monotonic() - started
        write(out / 'result.json', result)
        print(json.dumps({k: result[k] for k in ['actual_exit', 'native_pid', 'native_exit', 'timed_out', 'error'] if k in result}))
    return result['actual_exit']


if __name__ == '__main__':
    raise SystemExit(main())
