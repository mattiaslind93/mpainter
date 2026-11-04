"""Verify real Host Save/Open, saved source graphs and cancelled source Open.

Run inside the host's build environment with an already-built matching binary.
All application state and fixtures stay in a fresh output directory under build/.
No visible windows, build, retry or external Blender process is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-renderer', default='', help='Optional renderer substring')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    build = (repo.parent / 'build').resolve()
    host, environment, out = args.host.resolve(), args.environment.resolve(), args.output.resolve()
    if out.exists() or out == build or not out.is_relative_to(build):
        raise ValueError('Use a fresh private output directory under build/')
    run_host = repo / 'scripts/host/run-host.sh'
    guarded = [host, environment, Path(__file__).resolve(), run_host,
               repo / 'scripts/host/gpu-env.sh', repo / 'scripts/host/session_logging.py']
    before = {str(path): sha(path) for path in guarded}
    out.mkdir(parents=True)
    project = out / 'writer/packed-source.blend'
    result = {'kind': 'real-Host material source persistence', 'status': 'STARTED',
              'inputs_sha256': before, 'stages': [], 'maps_UI_enabled': False,
              'scope': 'Saved graph GPU correctness, one original payload, fresh Open, '
                       'two prepublication cancellation boundaries and repeat Save; '
                       'no stock Blender appearance, physical OOM or latency claim.'}
    started = time.monotonic()
    try:
        for mode in ('writer', 'cold'):
            child = out / mode
            child.mkdir()
            # This script runs inside the build environment. Application XDG
            # overrides never affect the caller's distrobox client.
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(('BTS_', 'XDG_', 'QT_', 'LC_'))}
            env.pop('WAYLAND_DISPLAY', None)
            env.update(BTS_ROOT=str(build.parent), BTS_HOST_EXECUTABLE=str(host),
                       BTS_ENVIRONMENT=str(environment), QT_QPA_PLATFORM='offscreen',
                       BTS_HEADLESS_WORKSPACE='1', BTS_MATERIAL_LAYER_SOURCE_GPU_TEST='1',
                       BTS_PD149_SOURCE_HOST_MODE=mode, LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8')
            for key, name in {
                'XDG_CONFIG_HOME': 'config', 'XDG_DATA_HOME': 'data',
                'XDG_STATE_HOME': 'state', 'XDG_CACHE_HOME': 'cache',
                'XDG_RUNTIME_DIR': 'runtime', 'TMPDIR': 'tmp',
                'BTS_RESOURCE_CACHE': 'resources', 'BTS_RECOVERY_DIR': 'recovery',
                'BTS_ASSET_LIBRARY_DIR': 'library', 'BTS_TEMPLATE_DIR': 'templates',
                'BTS_SESSION_LOG_DIR': 'logs', 'CUDA_CACHE_PATH': 'cuda-cache',
                '__GL_SHADER_DISK_CACHE_PATH': 'gl-cache',
            }.items():
                directory = child / name
                directory.mkdir(mode=0o700)
                env[key] = str(directory)
            command = ['nice', '-n', '19', 'bash', str(run_host), '--no-splash',
                       '--headless-viewport-test', str(child), '--open-document', str(project),
                       '--environment', str(environment), '--library-dir', str(child / 'library')]
            stage = {'mode': mode, 'command': command}
            result['stages'].append(stage)
            write(out / 'result.json', result)
            with (child / 'stdout.log').open('w') as log:
                process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    # EOF includes run-host's asynchronous timestamp logger,
                    # which can outlive the native process by a few writes.
                    output, _ = process.communicate(timeout=240)
                except BaseException:
                    # The wrapper execs the native child. Terminate only this
                    # private process group, including its logging helper.
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        output, _ = process.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        output, _ = process.communicate()
                    log.write(output.decode(errors='replace'))
                    raise
                log.write(output.decode(errors='replace'))
            stage['actual_exit'] = process.returncode
            text = (child / 'stdout.log').read_text(errors='replace')
            ready = re.findall(r'BTS_GPU_READY pid=(\d+)\b[^\n]*renderer=([^\n]+)', text)
            if len(ready) != 1:
                raise RuntimeError(f'{mode}: expected one native GPU-ready PID')
            pid, renderer = int(ready[0][0]), ready[0][1]
            stage.update(native_pid=pid, renderer=renderer)
            if args.require_renderer and args.require_renderer not in renderer:
                raise RuntimeError(f'{mode}: unexpected renderer {renderer}')
            if process.returncode or len(re.findall(r'BTS_SHUTDOWN clean pid=' + str(pid) + r'(?=\s|$)', text)) != 1:
                raise RuntimeError(f'{mode}: native failure or missing matching clean shutdown')
            if not re.search(r'BTS_SCENE_READY pid=' + str(pid) + r'\b[^\n]*native_window=none', text):
                raise RuntimeError(f'{mode}: missing offscreen native-window evidence')
            errors = re.findall(r'ID user (?:count|decrement) error|BTS_GPU_STACK_ERROR|'
                                r'BTS_PENDING_PROJECT_GPU_OWNER_CLEANUP_FAILED|BTS_FINALIZE_MS|'
                                r'BTS_FINALIZE_FALLBACK', text)
            stage['errors'] = errors
            if errors:
                raise RuntimeError(f'{mode}: native ownership/GPU-only path errors')
            proof_path = child / 'source-host.json'
            proof = json.loads(proof_path.read_text())
            if proof['native_pid'] != pid or proof['mode'] != mode or not proof['passed'] or not proof['owner_cleanup_complete']:
                raise RuntimeError(f'{mode}: missing matching native correctness evidence')
            stage.update(stdout_sha256=sha(child / 'stdout.log'), proof_sha256=sha(proof_path),
                         clean_shutdown=True)
            if mode == 'writer':
                if not proof.get('async_host_save') or not proof.get('first_draw_pixels_handles_preserved'):
                    raise RuntimeError('Writer missed actual Save or first-draw preservation')
                original = child / 'original.exr'
                if not project.is_file() or sha(original) != proof['original_sha256']:
                    raise RuntimeError('Writer project/source is missing or changed')
                original.rename(child / 'original.exr.hidden')
                stage['source_hidden_after_writer_exit'] = True
            else:
                if not all(proof.get(key) for key in ('fresh_source_hidden_open',
                           'same_staged_adopted_aliases', 'cancellation_tested',
                           'second_async_host_save')):
                    raise RuntimeError('Cold child missed Open, cancellation or repeat Save')
                for key in ('saved_detached_stock', 'resaved_detached_stock'):
                    stock = proof[key]
                    if (stock['original_packed_payload_count'] != 1 or stock['runtime_alias_count'] != 0
                            or not stock['carrier_raw_CPU_GPU_original_exact']
                            or not stock['live_owner_unchanged']):
                        raise RuntimeError(f'{key}: original source/stock graph audit failed')
        if result['stages'][0]['native_pid'] == result['stages'][1]['native_pid']:
            raise RuntimeError('Fresh-process check needs distinct native PIDs')
        result['status'] = 'PASS'
    except BaseException as error:
        result.update(status='FAIL', error=repr(error))
        raise
    finally:
        after = {str(path): sha(path) for path in guarded}
        result.update(elapsed_seconds=time.monotonic() - started, controls_equal=before == after,
                      post_inputs_sha256=after)
        if before != after:
            result.update(status='FAIL', error='Pinned runtime inputs changed')
        write(out / 'result.json', result)
        if before != after:
            raise RuntimeError('Pinned runtime inputs changed')


if __name__ == '__main__':
    main()
