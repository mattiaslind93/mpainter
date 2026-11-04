"""Explicit uniform EEVEE surface baseline: four real phases, strict ROI equality.

Source-only until separately admitted with the matching SurfaceGraph overlay.
No Cycles/BA, nonlinear/source-map or latency acceptance is implied.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import time


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(value, message):
    if not value:
        raise RuntimeError(message)


def uint(value):
    require(type(value) is str and re.fullmatch(r'0|[1-9][0-9]*', value), 'Native unsigned counter shape')
    number = int(value)
    require(number < 2**64, 'Native unsigned counter range')
    return number


def integer(value):
    require(type(value) is int and value >= 0, 'Native integer shape')
    return value


def f32(value):
    return struct.unpack('=f', struct.pack('=f', value))[0]


def reference_graph(baseline, phase):
    """Independent literal recipe; never read expected values from eval or AOVs."""
    graph = copy.deepcopy(baseline)
    if phase in (1, 4):
        return graph
    weights = (0.328125, 0.109375, 0.4375, 0.125)
    colors = ((.25, .5, .75), (.75, .25, .5), (.125, .75, .25), (.5, .125, .875))
    ao = sum(w*x for w, x in zip(weights, (1, .5, .75, .25)))
    rgb = [f32(sum(weights[i]*colors[i][c] for i in range(4)) * (.5 + .5*ao)) for c in range(3)]
    metal = f32(sum(w*x for w, x in zip(weights, (0, .25, .5, 1))))
    rough = f32(sum(w*x for w, x in zip(weights, (.125, .25, .5, .75))))
    if phase == 2:
        rgb = [1.0, 0.0, 1.0]  # Deliberately different public surface Color.
    for i in range(3):
        graph['inputs'][i]['incoming'] = []
        if i == 0:
            graph['inputs'][i]['value'][:3] = rgb
        else:
            graph['inputs'][i]['value'][0] = metal if i == 1 else rough
    return graph


def valid_graph(graph):
    require(type(graph) is dict and graph['valid'] is True, 'Canonical graph unavailable')
    for key, kind in [('output', 'ShaderNodeOutputMaterial'), ('principled', 'ShaderNodeBsdfPrincipled')]:
        require(graph[key]['type'] == kind and graph[key]['muted'] is False, 'Canonical node route differs')
    require(len(graph['inputs']) == 4 and graph['route'], 'Canonical graph socket shape')
    require(graph['route'][0]['identifier'] == 'Surface' and len(graph['route'][0]['incoming']) == 1, 'Actual Surface link missing')
    names = ['Base Color', 'Metallic', 'Roughness', 'Normal']
    for i, socket in enumerate(graph['inputs']):
        require(socket['identifier'] == names[i] and socket['type'] == [2, 0, 0, 1][i]
                and socket['components'] == [4, 1, 1, 3][i], 'Typed Principled input differs')
    for socket in graph['route'] + graph['inputs']:
        require(socket['available'] is True and len(socket['value']) == 4 and len(socket['incoming']) <= 1, 'Socket shape/availability')
        require(all(type(v) in (int, float) and math.isfinite(v) for v in socket['value']), 'Nonfinite graph defaults')
        for link in socket['incoming']:
            require(link['valid'] is True and link['muted'] is False and link['available'] is True
                    and link['from']['muted'] is False and link['to']['muted'] is False
                    and link['input'] == socket['identifier'] and link['output'], 'Inactive graph link')


def causal_sampling_prefix(observations, terminal, phase, previous_index):
    """Only unique-terminal PRECEDING rows may prove restart/progression."""
    occurrences = [i for i, row in enumerate(observations) if row == terminal]
    require(len(occurrences) == 1, 'Terminal must occur exactly once in actual trace')
    prefix = [row for row in observations[:occurrences[0]]
              if row.get('phase') == phase and row.get('graph_matches') is True]
    require(prefix and any(row['viewport_sampling_finished'] is False for row in prefix),
            'No preceding actual unfinished witness')
    indices = [uint(row['sample_index']) for row in prefix]
    require(any(b > a for a, b in zip(indices, indices[1:])), 'No preceding actual sample progression')
    if phase > 1:
        require(any(row['reset_pending'] is True or uint(row['sample_index']) < previous_index for row in prefix),
                'No preceding actual phase restart')
    return prefix


def verify_duplicate_readback(frame, output):
    phase = integer(frame['phase'])
    require(frame['raw'] == f'phase-{phase}.rgba8' and frame['duplicate_raw'] == f'phase-{phase}.duplicate.rgba8'
            and frame['duplicate_readback_no_pump'] is True, 'Canonical duplicate readback provenance')
    require(uint(frame['readback_draw_before']) == uint(frame['readback_draw_after']) == uint(frame['terminal']['host_draw'])
            and integer(frame['readback_frames_before']) == integer(frame['readback_frames_after']) == integer(frame['terminal']['frames_after']),
            'Duplicate readbacks advanced draw or Paint identity')
    first, second = output / frame['raw'], output / frame['duplicate_raw']
    require(sha(first) == frame['first_sha256'] and sha(second) == frame['duplicate_sha256'], 'Retained duplicate readback hashes')
    require(first.read_bytes() == second.read_bytes(), 'Consecutive FBO readbacks differ')


def verify_surface(proof, output, pid, noop=False, cold=False):
    require(not (noop and cold), "Surface controls must be isolated")
    profile = 'uniform-eevee-surface-cold-viewport-v1' if cold else 'uniform-eevee-surface-noop-repeatability-v1' if noop else 'uniform-eevee-surface-baseline-v1'
    require(proof['profile'] == profile and proof['passed'] is True
            and proof['error'] == '' and proof['native_pid'] == pid and proof['stage'] == (60 if noop else 6) and proof['phase'] == (2 if noop else 4),
            'Actor verdict/identity')
    require(proof['actual_product_import'] is True and proof['import_one_history_commit'] is True, 'Public Import path required')
    require(all(proof[k] is False for k in ['settings_written', 'sampling_reset_called', 'history_extension_tested'])
            and proof['timer_redraw_requests'] == 0 and proof['historical_BA_negatives_unchanged'] is True,
            'Test scope/settings changed')
    fixture = proof['fixture']
    for key, expected in {'package_version': 1, 'source_members_version': 3, 'uniform_inputs': 4,
                          'source_textures': 0, 'mask_resources': 3, 'logical_mask_members': 6,
                          'unique_payload_files': 3, 'width': 128, 'height': 128}.items():
        require(type(fixture[key]) is int and fixture[key] == expected, 'Fixture contract: ' + key)
    require(fixture['destination_tiles'] == [1001, 1002] and fixture['literal_masks'] == [.25, .5, .125]
            and fixture['runtime_mask_precision_proved_here'] is False, 'Encoded fixture is not a GPU precision proof')
    masks = proof['masks']
    require(integer(masks['components']) == 98304 and type(masks['maximum_error']) in (int, float)
            and masks['maximum_error'] == 0 and masks['required_exact'] is True and len(masks['formats']) == 3,
            'Actual literal mask planes differ')
    for row in masks['formats']:
        require(all(row[k] is True for k in ['L32F', 'nearest', 'alpha_channel_packed']), 'Actual mask precision/sampler contract')
    mask_bytes = (output / 'actual-mask-R.f32').read_bytes()
    require(len(mask_bytes) == 98304*4, 'Retained full mask plane bytes')
    values = struct.unpack('=98304f', mask_bytes)
    for stack, literal in enumerate((.25, .5, .125)):
        require(all(x == literal for x in values[stack*32768:(stack+1)*32768]), 'Retained full mask plane is not exact')
    phases, frames = proof['phases'], proof['frames']
    require(len(phases) == len(frames) == (2 if noop else 4) and proof['diagnostic_graph_invalidations'] == (1 if noop else 5 if cold else 4), 'Exact phases/invalidation count')
    first = phases[0]['terminal']
    baseline, slots = first['original_graph'], first['visible_slots']
    valid_graph(baseline)
    require([s['incoming'][0]['from']['name'] for s in baseline['inputs']]
            == ['__BTS_LayeredColor', '__BTS_LayeredChannel1', '__BTS_LayeredChannel2', '__BTS_LayeredBump'], 'Actual layered endpoints')
    selected = [s for s in slots if s['selected_binding'] is True]
    require(len({s['object_uid'] for s in selected}) == 2 and sum(s['faces'] for s in selected) == 4, 'Both actual selected owners required')
    target, generation, revision = first['selected_material_uid'], uint(first['scene_target_generation']), uint(first['revision_after'])
    previous_draw = 0
    cold_generations = []
    if cold:
        discovery = proof['discovery_terminal']
        require(discovery['terminal'] is True and discovery['graph_matches'] is True
                and discovery['status'] == 'Captured' and discovery['surface_graph_status_enum'] == 8
                and discovery['original_graph'] == discovery['evaluated_graph'] == discovery['expected_graph'] == baseline
                and discovery['visible_slots'] == discovery['expected_visible_slots'] == slots
                and discovery['selected_material_uid'] == target and discovery['viewport_sampling_finished'] is True
                and discovery['needed_shaders_loaded'] is True and discovery['interactive'] is False and discovery['reset_pending'] is False
                and discovery['queued_shaders'] == discovery['queued_textures'] == 0, 'Actual completed Import discovery required')
        causal_sampling_prefix(proof['discovery_observations'], discovery, 1, 0)
        previous_draw = uint(discovery['host_draw'])
        previous_generation = uint(discovery['scene_target_generation'])
        require(len(proof['cold_viewport_generations']) == 4, 'Four fresh viewport lifecycles required')
        for index, lifecycle in enumerate(proof['cold_viewport_generations'], 1):
            new_generation = uint(lifecycle['generation'])
            require(lifecycle['phase'] == index and new_generation > previous_generation,
                    'Cold viewport generations must strictly increase after discovery')
            cold_generations.append(new_generation)
            previous_generation = new_generation
    for index, phase in enumerate(phases, 1):
        if cold:
            generation = cold_generations[index-1]
            require(uint(proof['cold_viewport_generations'][index-1]['draw_floor']) == uint(phase['terminal']['phase_draw_floor']),
                    'Cold lifecycle/Paint floor differs')
        row = phase['terminal']
        require(phase['phase'] == index and row['phase'] == index and row['terminal'] is True
                and phase['cumulative_invalidations'] == ([0, 1] if noop else [1, 2, 4, 5] if cold else [0, 1, 3, 4])[index-1], 'Per-phase completion/invalidation identity')
        expected = baseline if noop else reference_graph(baseline, index)
        require(row['original_graph'] == row['evaluated_graph'] == row['expected_graph'] == expected, 'Independent current original/evaluated reference differs')
        valid_graph(row['original_graph'])
        require(row['surface_graph_status_enum'] == 8, 'Captured graph status required')
        route = row['host_backend_route']
        require(route.get('active') is False and route.get('device') == route.get('status') == 'EEVEE', 'Actual EEVEE route required')
        require(row['visible_slots'] == row['expected_visible_slots'] == slots
                and row['selected_material_uid'] == target and target > 0, 'Evaluated selected binding changed')
        require(all(row[k] is True for k in ['graph_matches', 'distinct_evaluated', 'original_backlink_matches',
                    'distinct_evaluated_tree', 'tree_original_backlink_matches']) and row['material_override'] is False,
                'Actual evaluated graph ownership missing')
        draw = uint(row['host_draw'])
        require(draw == uint(row['graph_host_draw']) > max(previous_draw, uint(row['draw_before']), uint(row['phase_draw_floor']))
                and generation == uint(row['graph_target_generation']) == uint(row['scene_target_generation'])
                and revision == uint(row['revision_before']) == uint(row['revision_after']) > 0
                and integer(row['frames_after']) > integer(row['frames_before']), 'Fresh phase-specific postPaint identity')
        require(row['status'] == 'Captured' and row['viewport_sampling_finished'] is True
                and row['needed_shaders_loaded'] is True and row['render_extent_valid'] is True
                and row['interactive'] is False and row['reset_pending'] is False
                and row['queued_shaders'] == row['queued_textures'] == 0, 'Actual finished EEVEE sampling/queues')
        require(uint(row['sample_count']) > 0, 'Configured EEVEE samples missing')
        previous_index = uint(phases[index-2]['terminal']['sample_index']) if index > 1 else 0
        prefix = causal_sampling_prefix(proof['observations'], row, index, previous_index)
        prefix_draw = prefix_ticket = 0
        for observed in prefix:
            require(observed['original_graph'] == observed['evaluated_graph'] == observed['expected_graph'] == expected
                    and observed['selected_material_uid'] == target
                    and observed['visible_slots'] == observed['expected_visible_slots'] == slots,
                    'Causal prefix graph/binding differs')
            observed_draw, observed_ticket = uint(observed['host_draw']), uint(observed['paint_sequence'])
            require(prefix_draw < observed_draw < draw and prefix_ticket < observed_ticket < uint(row['paint_sequence'])
                    and observed_draw > max(uint(observed['draw_before']), uint(observed['phase_draw_floor']))
                    and uint(observed['graph_host_draw']) == observed_draw
                    and generation == uint(observed['graph_target_generation']) == uint(observed['scene_target_generation'])
                    and revision == uint(observed['revision_before']) == uint(observed['revision_after'])
                    and integer(observed['frames_after']) > integer(observed['frames_before']),
                    'Causal prefix draw/Paint/target/revision differs')
            prefix_draw, prefix_ticket = observed_draw, observed_ticket
        previous_draw = draw
        require(frames[index-1]['phase'] == index and frames[index-1]['terminal'] == row
                and frames[index-1]['raw_saved_before_pixel_assertions'] is True, 'Frame/terminal identity')
    width, height = integer(frames[0]['width']), integer(frames[0]['height'])
    require(width > 0 and height > 0, 'Empty framebuffer')
    roi = (output / 'interior-roi.u8').read_bytes()
    require(len(roi) == width*height and set(roi) <= {0, 1, 2}
            and [roi.count(1), roi.count(2)] == proof['roi_counts'] and min(proof['roi_counts']) > 0, 'Actual two-tile interior ROI')
    images = []
    for frame in frames:
        require(frame['width'] == width and frame['height'] == height, 'Framebuffer extent changed')
        require(frame['raw'] == 'phase-' + str(frame['phase']) + '.rgba8', 'Canonical raw capture path')
        data = (output / frame['raw']).read_bytes()
        require(len(data) == width*height*4, 'Raw capture extent')
        if noop or cold:
            verify_duplicate_readback(frame, output)
        if cold:
            require(re.fullmatch('[a-f0-9]{64}', proof['cold_projection_sha256']) is not None
                    and frame['projected_fixture_sha256'] == proof['cold_projection_sha256']
                    and frame['environment'] == proof['cold_environment'] and proof['cold_environment'],
                    'Actual cold fixture projection/environment changed')
        images.append(data)
    signal = [False, False]
    for at, tile in enumerate(roi):
        if not tile:
            continue
        pixels = [data[at*4:at*4+4] for data in images]
        require(all(p[3] == 255 for p in pixels), 'Nonopaque interior surface')
        if noop:
            require(pixels[0][:3] == pixels[1][:3], 'Strict no-op original-graph RGB8 mismatch')
        else:
            signal[tile-1] |= pixels[0][:3] != pixels[1][:3]
            require(pixels[0][:3] == pixels[2][:3] == pixels[3][:3], 'Strict same-backend RGB8 interior mismatch')
    if noop:
        return {'classification': 'no-op original-graph repeatability and duplicate readback only', 'phases': 2,
                'mask_components': 98304, 'mask_maximum_error': 0, 'roi_counts': proof['roi_counts'], 'strict_rgb8_error': 0, 'duplicate_readbacks_equal': True}
    require(all(signal), 'Different Color liveness missing on one tile')
    if cold:
        return {'classification': 'symmetric cold viewport reference control only; not live parity', 'phases': 4,
                'mask_components': 98304, 'mask_maximum_error': 0, 'roi_counts': proof['roi_counts'], 'strict_rgb8_error': 0,
                'different_color_signal': signal, 'duplicate_readbacks_equal': True, 'viewport_generations': cold_generations}
    return {'phases': 4, 'mask_components': 98304, 'mask_maximum_error': 0,
            'roi_counts': proof['roi_counts'], 'strict_rgb8_error': 0, 'different_color_signal': signal}


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
    try:
        return child.communicate(timeout=timeout)[0], False
    except subprocess.TimeoutExpired:
        groups = set()
        for pid in descendants(child.pid):
            try:
                groups.add(os.getpgid(pid))
            except ProcessLookupError:
                pass
        require(os.getpgrp() not in groups, 'Refuse to terminate verifier group')
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


def write_fixture(output, build):
    require(output.resolve().is_relative_to(build), 'Fixture under private build only')
    output.mkdir()
    vertices, uvs, faces = [], [], []
    for owner, (tile, offset) in enumerate(((1001, 0), (1002, 2))):
        base = 4*owner + 1
        vertices.extend(f'v {offset+x} {y} 0' for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)))
        uvs.extend(f'vt {tile-1001+x} {y}' for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)))
        faces.extend((f'o SurfaceOwner{owner}', 'usemtl Surface',
                      f'f {base}/{base} {base+1}/{base+1} {base+2}/{base+2}',
                      f'f {base}/{base} {base+2}/{base+2} {base+3}/{base+3}'))
    # Own synthetic canonical native declaration only; no live gate bypass.
    (output / 'surface.mtl').write_text('newmtl Surface\nKd 1 1 1\nKs .5 .5 .5\nNs 16\n')
    path = output / 'surface.obj'
    path.write_text('\n'.join(('mtllib surface.mtl', *vertices, *uvs, *faces)) + '\n')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--host-sha256', required=True)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    control = parser.add_mutually_exclusive_group()
    control.add_argument('--noop-control', action='store_true', help='Separate two-phase unchanged-graph repeatability control, not surface parity')
    control.add_argument('--cold-control', action='store_true', help='Symmetric fresh-viewport history-isolation control; not live parity')
    args = parser.parse_args()
    require(os.environ.get('CONTAINER_ID') or Path('/run/.containerenv').exists(), 'Enter bts-build')
    require(os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run nice19')
    source = args.source_root.resolve()
    build = (Path(os.environ.get('BTS_ROOT', '/home/mlind/bts')) / 'build').resolve()
    out = args.output.resolve()
    require(out.is_relative_to(build) and out != build and not out.exists(), 'Fresh private output under build')
    require(re.fullmatch(r'[a-f0-9]{64}', args.host_sha256), 'Qualified host hash')
    out.mkdir(parents=True)
    started = time.monotonic()
    result = {'actual_exit': 1, 'mode': 'cold-control' if args.cold_control else 'noop-control' if args.noop_control else 'surface', 'scope': 'Symmetric cold viewport history-isolation control only; not live parity or a production fix' if args.cold_control else 'No-op graph repeatability and duplicate FBO readback only; not surface parity' if args.noop_control else 'Uniform same-backend EEVEE surface baseline only; no Cycles/BA or latency claim'}
    controls = {}
    def write(path, value): path.write_text(json.dumps(value, indent=2) + '\n')
    def guard():
        for path, expected in controls.items():
            require(Path(path).is_file() and sha(path) == expected, 'Input drift: ' + path)
    try:
        host, environment = args.host.resolve(), args.environment.resolve()
        require(sha(host) == args.host_sha256, 'Qualified host mismatch')
        controls[str(host)] = args.host_sha256
        for path in [environment, Path(__file__).resolve(), *[source / rel for rel in [
                'native/tests/MaterialLayerSurfaceReference.hh', 'native/tests/MaterialLayerSurfaceFixture.hh',
                'native/tests/MaterialLayerSurfaceArtist.inc', 'native/tests/MaterialLayerSurfaceGraphObservation.hh',
                'native/host/SurfaceGraphSnapshot.hh', 'native/host/SurfaceGraphSnapshot.cc',
                'native/tests/material_layering_ui_artist_test.cc',
                'scripts/host/run-host.sh', 'scripts/host/gpu-env.sh', 'scripts/host/session_logging.py']]]:
            controls[str(path)] = sha(path)
        mesh = write_fixture(out / 'fixture', build)
        mtl = mesh.with_suffix('.mtl')
        controls.update({str(p): sha(p) for p in (mesh, mtl)})
        output = out / 'output'; output.mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_USER_'))}
        require(env.get('DISPLAY'), 'Proven unmapped GLX DISPLAY/XAUTHORITY required')
        env.pop('WAYLAND_DISPLAY', None)
        env.update(BTS_ROOT=str(build.parent), BTS_HOST_EXECUTABLE=str(host), BTS_ENVIRONMENT=str(environment),
                   BTS_HEADLESS_WORKSPACE='1', BTS_PD149_SURFACE_TEST='1', BTS_PD149_SURFACE_GRAPH_TEST='1',
                   QT_QPA_PLATFORM='offscreen', LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8',
                   PYTHONDONTWRITEBYTECODE='1', LD_LIBRARY_PATH=str(host.parent / 'lib'))
        if args.noop_control:
            env['BTS_PD149_SURFACE_NOOP_CONTROL'] = '1'
        if args.cold_control:
            env['BTS_PD149_SURFACE_COLD_CONTROL'] = '1'
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
        write(out / 'admission.json', {'command': command, 'controls': controls, 'timeout_seconds': 360,
              'test_only': True, 'sampling_settings_written': False, 'actor_timer_redraw': False})
        guard()
        child = subprocess.Popen(command, cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        result.update(native_pid=child.pid, binary_sha256=args.host_sha256)
        write(out / 'running.json', result)
        raw, timed_out = communicate_owned(child, 360)
        (out / 'run.log').write_bytes(raw)
        text = raw.decode(errors='replace')
        clean = re.findall(r'\bBTS_SHUTDOWN clean pid=(\d+)\b', text)
        renderer = re.findall(r'BTS_GPU_READY pid=' + str(child.pid) + r' renderer=(.*?) qt=', text)
        forbidden = re.findall(r'BTS_DRAW_FAILED|BTS_EXIT_FAILED|Shader compile failed|Segmentation fault|AddressSanitizer|LeakSanitizer|ID.user.(?:count|decrement).*error|users on ID|PD149_SURFACE(?:_NOOP|_COLD)? FAILED|SURFACE.*restoration', text, re.I)
        result.update(native_exit=child.returncode, timed_out=timed_out, matching_clean=clean == [str(child.pid)], renderer=renderer, forbidden=forbidden)
        require(not timed_out and child.returncode == 0 and result['matching_clean'], 'Native OS/CLEAN negative retained')
        require(len(renderer) == 1 and 'NVIDIA' in renderer[0] and 'RTX' in renderer[0], 'Offscreen RTX required')
        require(re.search(r'BTS_SCENE_READY pid=' + str(child.pid) + r' .*?native_window=none\b', text)
                and not forbidden and text.count('PD149_SURFACE_COLD PASS' if args.cold_control else 'PD149_SURFACE_NOOP PASS' if args.noop_control else 'PD149_SURFACE PASS') == 1, 'Unmapped/forbidden/actor negative')
        proof_path = output / 'pd149-surface.json'
        result['proof_sha256'] = sha(proof_path)
        result['surface'] = verify_surface(json.loads(proof_path.read_text()), output, child.pid, args.noop_control, args.cold_control)
        guard(); result['actual_exit'] = 0
    except BaseException as error:
        result['error'] = repr(error)
    finally:
        try: guard(); result['postguards_passed'] = True
        except BaseException as error: result.update(actual_exit=1, postguards_passed=False, postguard_error=repr(error))
        result['seconds'] = time.monotonic() - started
        write(out / 'result.json', result)
    return result['actual_exit']


if __name__ == '__main__':
    raise SystemExit(main())
