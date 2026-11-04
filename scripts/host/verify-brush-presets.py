"""BO real Shelf/editor acceptance, followed by an independent process restart.

Run inside bts-build with nice -n 19. Each phase owns a private Xvfb/EGL
display and fresh preferences, shader/resource caches and recovery directory.
Only the explicit test User library is shared between the two processes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from private_gpu_gui import private_gpu_gui


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    base = root.parent
    output = args.output.resolve()
    assert output.is_relative_to(base / 'build'), 'Use an owned build output'
    assert not output.exists(), 'Preserve previous evidence; use a new output'
    assert Path('/run/.containerenv').exists(), 'Enter bts-build first'
    assert os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run with nice -n 19'
    output.mkdir(parents=True)
    binary = args.binary.resolve()
    binary_sha = sha(binary)
    library = output / 'user-library'
    library.mkdir()
    receipts = []
    for phase in ('warm', 'cold'):
        out = output / phase
        out.mkdir()
        environment = {
            key: value for key, value in os.environ.items()
            if not key.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_'))
            and key not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')
        }
        environment.update(
            BTS_ROOT=str(base), BTS_HOST_EXECUTABLE=str(binary),
            BTS_ENVIRONMENT=str(args.environment.resolve()),
            BTS_BO_PRESET_PHASE=phase, BTS_BO_PRESET_WARM=str(output / 'warm'),
            BTS_SESSION_LOG_DIR=str(out / 'logs'),
            BTS_TEMPLATE_DIR=str(out / 'templates'),
            BTS_PERFORMANCE_PROFILE='1', LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8',
        )
        for key, folder in (
                ('BLENDER_USER_CONFIG', 'blender-config'),
                ('CUDA_CACHE_PATH', 'cuda-cache'),
                ('__GL_SHADER_DISK_CACHE_PATH', 'nvidia-cache')):
            (out / folder).mkdir()
            environment[key] = str(out / folder)
        started = time.monotonic()
        with private_gpu_gui(out / 'private', environment, root) as (prefix, child):
            command = prefix + [
                'nice', '-n', '19', 'bash', str(root / 'scripts/host/run-host.sh'),
                '--no-splash', '--workspace', '--brush-presets-demo', str(out),
                '--brush-presets-mesh', str(root / 'fixtures/single_tile.obj'),
                '--autosave-seconds', '3600', '--library-dir', str(library),
            ]
            (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
            with (out / 'run.log').open('w') as log:
                process = subprocess.Popen(command, env=child, stdout=log,
                                           stderr=subprocess.STDOUT)
                (out / 'pid.json').write_text(json.dumps({'pid': process.pid}) + '\n')
                timed_out = False
                try:
                    code = process.wait(timeout=480)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    process.terminate()
                    try:
                        code = process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        code = process.wait()
            text = (out / 'run.log').read_text(errors='replace')
            receipt = {
                'phase': phase, 'actual_exit': code, 'pid': process.pid,
                'timed_out': timed_out, 'seconds': time.monotonic() - started,
                'binary_sha256': binary_sha, 'binary_unchanged': sha(binary) == binary_sha,
                'clean_exit': 'BTS_SHUTDOWN clean' in text,
                'display': json.loads((out / 'private/private-display.json').read_text()),
                'command': command,
            }
            receipts.append(receipt)
            (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        # Preserve actual process failures before inspecting semantic proof.
        assert code == 0 and not timed_out, receipt
        assert receipt['clean_exit'] and receipt['binary_unchanged'], receipt
        assert not re.search(
            r'BTS_HOST_FAILED|BTS_DRAW_FAILED|BTS_GPU_AUTHORING_CPU_READ|'
            r'BTS_GPU_CPU_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_THUMBNAIL_ERROR|'
            r'Shader compil(?:ation|e) failed', text, re.I), out / 'run.log'
        proof = json.loads((out / 'brush-presets.json').read_text())
        assert not proof['failure'] and proof['phase'] == phase, proof
        assert 'BTS_BO_BRUSH_PRESETS_PASS' in text
        assert proof['expected_brush_b64'] and proof['imported_id']
        if phase == 'warm':
            assert len(proof['checks']) >= 18, proof['checks']
            thumbs = proof['thumbnails']
            assert len(thumbs['completed']) == 9
            assert thumbs['gpu_image_hashes'] == thumbs['display_icon_hashes'] == 9
            assert thumbs['diagnostics']['cpu_materializations'] == 0
            assert (out / 'custom.btsbrush').is_file()
            assert (out / 'brushes-shelf.png').is_file()
        else:
            warm = json.loads((output / 'warm/brush-presets.json').read_text())
            assert proof['pid'] != warm['pid'], 'Cold must be a new app process'
            for key in ('expected_brush_b64', 'imported_id', 'original_id'):
                assert proof[key] == warm[key], key
            assert (out / 'brushes-cold.png').is_file()
    summary = {
        'binary_sha256': binary_sha, 'phases': receipts,
        'scope': 'Real warm GUI editors and independent cold global User library. '
                 'GPU thumbnail packets prove displayed stroke rendering; '
                 'this is a functional lifecycle test, not a stroke latency benchmark.',
    }
    (output / 'receipt.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
