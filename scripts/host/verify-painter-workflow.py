# SPDX-License-Identifier: GPL-3.0-or-later
"""Production dark UI proof on private native GPU surfaces, never the desktop."""
import argparse, hashlib, json, os, re, signal, subprocess, sys
from pathlib import Path
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
p.add_argument('--before', action='store_true')
p.add_argument('--existing', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
base = root.parent
out = a.output.resolve()
assert out.is_relative_to(base / 'build'), 'Use a private build output directory'
assert Path('/run/.containerenv').exists(), 'Enter bts-build before running native commands'
assert os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run the verifier with nice 19'
if not a.existing:
    assert not out.exists(), 'Preserve earlier evidence; choose a new output directory'
    out.mkdir(parents=True)
result = {'actual_exit': None, 'before': a.before}

def save():
    (out / 'verified.json').write_text(json.dumps(result, indent=2) + '\n')

try:
    if not a.existing:
        from private_gpu_gui import private_gpu_gui
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_'))
               and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')}
        env.update(BTS_ROOT=str(base), BTS_HOST_EXECUTABLE=str(a.binary.resolve()),
                   BTS_ENVIRONMENT=str(a.environment.resolve()), LANG='sv_SE.UTF-8',
                   LC_ALL='sv_SE.UTF-8', BTS_SESSION_LOG_DIR=str(out / 'logs'),
                   BTS_TEMPLATE_DIR=str(out / 'templates'))
        if a.before:
            env.update(BTS_BAKING_LAYOUT_DEMO='1', BTS_BAKING_LAYOUT_BEFORE='1')
        else:
            env['BTS_PAINTER_WORKFLOW_DEMO'] = '1'
        with private_gpu_gui(out, env, root) as (prefix, private):
            command = prefix + ['nice', '-n', '19', 'bash', str(root / 'scripts/host/run-host.sh'),
                                '--no-splash', '--workspace', '--gui-fix-demo', str(out),
                                '--gui-fix-mesh', str(root / 'native/tests/fixtures/single_tile.obj'),
                                '--gui-fix-resolution', '128', '--autosave-seconds', '3600',
                                '--library-dir', str(out / 'library')]
            (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
            with (out / 'run.log').open('w') as log:
                process = subprocess.Popen(command, env=private, stdout=log,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                result['launcher_pid'] = process.pid
                try:
                    code = process.wait(timeout=300)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        code = process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        code = process.wait(timeout=10)
                    result['timed_out'] = True
                result['actual_exit'] = code
                save()
                assert code == 0, f'Native actor exited {code}; retained run.log'
    else:
        previous = json.loads((out / 'verified.json').read_text())
        result.update({k: previous[k] for k in ('actual_exit', 'launcher_pid') if k in previous})
        assert result['actual_exit'] == 0, 'The retained run did not exit successfully'

    log = (out / 'run.log').read_text(errors='replace')
    assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compil(?:ation|e) failed|'
                         r'BTS_GPU_CPU_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_THUMBNAIL_ERROR', log, re.I)
    assert 'BTS_SHUTDOWN clean' in log
    display = json.loads((out / 'private-display.json').read_text())
    assert display['renderer'] == 'NVIDIA GeForce RTX 3090' and not display['desktop_input']
    assert display['display'] != ':0'
    proof = json.loads((out / ('baking-layout.json' if a.before else 'painter-workflow.json')).read_text())
    captures = {c['stage']: c for c in proof['captures']}
    names = ['painting'] if a.before else [
        'layer-stack', 'generator', 'mask-levels', 'filter', 'mask-fill', 'paint-effect', 'mask-group',
        'nested-generator', 'nested-filter', 'nested-fill', 'nested-levels', 'nested-paint',
        'fill-channels', 'fill', 'content-levels', 'content-fill', 'folder', 'paint-layer',
        'smart-materials', 'smart-masks', 'baking']
    measured = []
    for width, height in [(1024, 768), (1920, 1080)]:
        for name in names:
            stage = f'{name}-{width}'
            c = captures[stage]
            assert (c['window_width'], c['window_height']) == (width, height), stage
            with Image.open(out / f'{stage}.png') as image:
                assert image.size == (width, height), (stage, image.size)
                band = list(image.convert('RGB').crop((0, 0, width, 60)).getdata())
                dark = sum(max(pixel) < 120 for pixel in band) / len(band)
                assert dark > .65, (stage, 'production dark chrome absent', dark)
            if not a.before:
                assert not c['layer_thumbnails_pending'] and not c['shader_failures'], stage
                assert len(c['material_thumbnail_sha256']) == 64 and len(c['mask_thumbnail_sha256']) == 64
                ids = [x['id'] for x in c['artist_channels']]
                assert 3 not in ids and 4 in ids and 14 in ids, (stage, ids)
                assert 'No properties' not in c['properties_title']
                assert all(0 <= int(row['opacity']) <= 100 and '.' not in row['opacity'] for row in c['rows'])
                assert all(row['height'] == (26 if row['effect'] else 44) for row in c['rows'])
                assert all(card['name'] in card['tooltip'] for card in c['shelf_cards'])
                if name in ('smart-materials', 'smart-masks'):
                    assert c['shelf_cards'] and c['shelf_grid'] == [96, 108], stage
                    assert c['shelf_category'] == ('Smart materials' if name == 'smart-materials' else 'Smart masks')
                if name.startswith('nested-'):
                    assert c['selected_mask'] and c['selected_effect'], stage
                    selected = [r for r in c['rows'] if r['effect'] == c['selected_effect']]
                    assert len(selected) == 1 and selected[0]['depth'] >= 3, stage
                    assert selected[0]['mask_effect'], stage
                if name == 'baking':
                    assert c['low_poly_visible'] and c['low_poly_checked']
                    assert c['low_poly_text'] == 'Use Low Poly Mesh as High Poly Mesh'
                    assert c['low_poly_common_parent'] == 'bakingCommonParameters'
                if name == 'layer-stack':
                    assert c['layer_list_height'] >= 200, stage
                    assert any(r['visible'] and r['has_mask'] and not r['effect'] for r in c['rows']), stage
                    assert any(r['visible'] and r['effect'] and r['depth'] >= 2 for r in c['rows']), stage
                if name == 'fill':
                    assert c['bump_value'] == .5 and c['bump_display'] == '0.500', stage
                if name in ('fill', 'fill-channels', 'paint-layer'):
                    buttons = {x['name']: x for x in c['channel_buttons']}
                    prefix = 'fillChannelEnabled' if name.startswith('fill') else 'brushChannel'
                    assert not buttons[prefix + '3']['visible'] and buttons[prefix + '4']['visible']
                    assert buttons[prefix + '0']['checked'] and not buttons[prefix + '1']['checked']
                    if name == 'fill-channels':
                        assert buttons[prefix + '0']['within_view'] and buttons[prefix + '1']['within_view'], stage
                    def rgb(value):
                        return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5))
                    enabled = rgb(buttons[prefix + '0']['background'])
                    disabled = rgb(buttons[prefix + '1']['background'])
                    assert enabled[2] > enabled[0] + 20, (stage, 'enabled button is not blue', enabled)
                    assert max(disabled) - min(disabled) < 16, (stage, 'disabled button is not gray', disabled)
                measured.append({'stage': stage, 'window': [width, height],
                                 'dark_chrome_fraction': dark, 'properties': c['visible_properties']})
    if not a.before:
        assert proof['nested_undo_verified'] and proof['nested_persistence_verified'] and proof['nested_paint_verified']
        assert proof['fractional_opacity_popup_unchanged'], 'Opening integer popup changed authored fractional opacity'
        assert [x['stage'] for x in proof['rename_checks']] == ['effect-focus-out-undo-redo', 'anchor-enter', 'anchor-focus-out-undo-redo', 'packed-open']
        assert [x['stage'] for x in proof['nested_gpu_checks']] == ['before-dab', 'after-dab', 'undo-dab', 'redo-dab', 'reopen-dab']
        assert [x['stroke_operations'] for x in proof['nested_gpu_checks']] == [0, 1, 0, 1, 1]
        assert proof['nested_paint_history_seed'].startswith('One empty opt-in StrokeHistory')
        assert all(abs(x['actual'] - x['expected']) <= x['tolerance'] and x['coverage'] >= .999 and not x['cpu_composite'] for x in proof['nested_gpu_checks'])
        native_pid = proof['pid']
        assert f'BTS_SHUTDOWN clean pid={native_pid}' in log
        assert 'BTS_GPU_THUMBNAIL_READY' in log
        assert len(captures) == len(names) * 2
        result['native_pid'] = native_pid
        result['typed_import_only_fixture'] = proof['typed_import_only_fixture']
    result.update(passed=True, private_display=display, captures=measured,
                  binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
                  scope='Measured production UI states and native GPU thumbnail packets; no global latency claim')
except BaseException as error:
    result.update(passed=False, error=repr(error))
    save()
    raise
else:
    save()
    print(json.dumps(result, indent=2))
