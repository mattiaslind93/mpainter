"""Private explicit image Raw Height actor; runtime only, never builds/installs.

Invoke inside bts-build with nice19 after a coherent approved actor build.
Warm and fresh cold must share the exact binary and frozen source manifest.
"""
import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from private_gpu_gui import private_gpu_gui

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--expected-binary-sha256', required=True)
p.add_argument('--source-manifest', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--phase', choices=['cpu', 'warm', 'cold'], required=True)
p.add_argument('--warm', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
build = root.parent / 'build'
def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
inside = lambda path: Path(path).resolve().is_relative_to(build.resolve())
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS, 0) == 19
out = a.output.resolve()
assert inside(out) and not out.exists(), 'New owned build output required'
binary = a.binary.resolve()
assert inside(binary) and sha(binary) == a.expected_binary_sha256
assert a.environment.is_file(), 'Explicit approved HDRI required'
manifest = json.loads(a.source_manifest.read_text())['files']
hashes = {name: value if isinstance(value, str) else value['sha256'] for name, value in manifest.items()}
assert {'native/tests/native_height_image_native_test.cc', 'native/tests/NativeHeightImageFixture.hh',
        'native/tests/NativeHeightImageReference.hh', 'scripts/host/verify-native-height-images.py'} <= hashes.keys()
assert all(not Path(name).is_absolute() and '..' not in Path(name).parts for name in hashes)
assert all(sha(root / name) == value for name, value in hashes.items())
out.mkdir(parents=True)
(out / 'source-guard.json').write_text(json.dumps(hashes, indent=2) + '\n')
base = out / 'private'
env = os.environ.copy()
for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD'):
    env.pop(key, None)
for key, folder in {'XDG_CONFIG_HOME':'config', 'XDG_DATA_HOME':'data', 'XDG_CACHE_HOME':'cache',
                    'XDG_RUNTIME_DIR':'runtime', 'TMPDIR':'tmp', 'BTS_RECOVERY_DIR':'recovery',
                    'BTS_RESOURCE_CACHE':'resources', 'BLENDER_USER_CONFIG':'blender-config',
                    'CUDA_CACHE_PATH':'cuda-cache', '__GL_SHADER_DISK_CACHE_PATH':'nvidia-cache'}.items():
    path = base / folder
    path.mkdir(parents=True, exist_ok=True)
    env[key] = str(path)
(base / 'runtime').chmod(0o700)
env.update(BTS_ROOT=str(root.parent), BTS_ENVIRONMENT=str(a.environment.resolve()),
           QT_QPA_PLATFORM='offscreen', QT_PLUGIN_PATH=str(root.parent / 'qt/6.8.3/plugins'),
           LD_LIBRARY_PATH=str(root.parent / 'qt/6.8.3/lib') + ':' + env.get('LD_LIBRARY_PATH', ''))
data = out / 'data'
hidden = []
record = {'phase': a.phase, 'binary_sha256': sha(binary), 'source_manifest_sha256': sha(a.source_manifest),
          'actual_exit': None, 'passed': False, 'clean': False}
try:
    if a.phase == 'cold':
        assert a.warm is not None
        warm = a.warm.resolve()
        assert inside(warm) and warm != out and not warm.is_relative_to(out)
        prior = json.loads((warm / 'receipt.json').read_text())
        assert prior['phase'] == 'warm' and prior['passed'] and prior['actual_exit'] == 0 and prior['clean']
        assert prior['binary_sha256'] == a.expected_binary_sha256
        assert prior['source_manifest_sha256'] == record['source_manifest_sha256']
        data = warm / 'data'
        assert not (data / 'image-cold.json').exists() and not (data / 'image-cold-data.json').exists(), 'Preserve prior cold evidence; use a fresh warm cohort'
        oracle = json.loads((data / 'image-restart-oracle.json').read_text())
        assert sha(data / 'packed-image-height.blend') == oracle['project_sha256']
        # All renames are confined to this passed owned warm output. Restore
        # even on native failure; never clear a process/global/default cache.
        for path in [data / 'source', data / 'samplers', warm / 'private/resources', warm / 'private/cache']:
            assert path.is_dir() and not path.is_symlink() and inside(path)
            held = path.with_name(path.name + '.height-cold-held')
            assert not held.exists()
            path.rename(held)
            hidden.append((path, held))
        assert not list((base / 'resources').iterdir())
        record['hidden_owned_paths'] = [str(path) for path, _ in hidden]
    args = [str(binary), str(data)] + (['--cpu-only'] if a.phase == 'cpu' else ['--cold-only'] if a.phase == 'cold' else [])
    display = nullcontext(([], env)) if a.phase == 'cpu' else private_gpu_gui(base, env, root)
    with display as (prefix, child_env):
        command = [*prefix, *args]
        record['command'] = command
        start = time.monotonic()
        with (out / 'native.log').open('w') as log:
            process = subprocess.Popen(command, env=child_env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            record['pid'] = process.pid
            (out / 'pid.json').write_text(json.dumps(record, indent=2) + '\n')
            try:
                process.wait(timeout=900)
            except subprocess.TimeoutExpired:
                record['timeout'] = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        record.update(actual_exit=process.returncode, seconds=time.monotonic() - start)
        logtext = (out / 'native.log').read_text(errors='replace')
        record['clean'] = 'BTS_HEIGHT_IMAGE_CLEAN' in logtext
        marker = 'HEIGHT_IMAGE_' + a.phase.upper() + ' PASS'
        record['verified_marker'] = marker in logtext
        proof = data / ('image-' + a.phase + '.json')
        record['native_proof'] = str(proof)
        assert process.returncode == 0 and record['clean'] and record['verified_marker'] and proof.is_file()
    assert sha(binary) == a.expected_binary_sha256
    assert all(sha(root / name) == value for name, value in hashes.items())
    record['passed'] = True
finally:
    conflicts = []
    for path, held in reversed(hidden):
        if path.exists() or not held.is_dir():
            conflicts.append(str(path))
        else:
            held.rename(path)
    record['owned_paths_restored'] = bool(hidden) and not conflicts
    record['restore_conflicts'] = conflicts
    if conflicts:
        record['passed'] = False
    (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
    assert not conflicts, 'Owned source/cache restore conflict; both paths preserved'
