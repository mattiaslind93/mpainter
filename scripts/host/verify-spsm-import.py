"""Direct original-source SPSM import via artist controls, history and packed Open."""
import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--fixtures', type=Path, required=True)
parser.add_argument('--roughness-bitmaps', action='store_true', help='Explicitly choose the tested raw-R/little-endian/first-row-V0/repeat-HQ bitmap contract')
parser.add_argument('--bitmap-color', choices=['linear-rec709', 'acescg'], help='Explicit native linear RGB/ordered RGB Levels/ignore-alpha interpretation; source appearance remains unverified')
parser.add_argument('--native-levels', action='store_true', help='Explicitly choose the validated native scalar Levels interpretation')
parser.add_argument('--native-bitmap-mask', action='store_true', help='Explicitly choose the compatible ordered native bitmap mask Fill/Levels interpretation')
parser.add_argument('--legacy-debug', action='store_true', help='Run obsolete optional-policy diagnostic branches; no current-policy pass is implied')
parser.add_argument('--binary', type=Path)
parser.add_argument('--environment', type=Path)
parser.add_argument('--format', choices=['obj','fbx'], default='obj')
a = parser.parse_args()
if not a.legacy_debug:
    if a.roughness_bitmaps or a.bitmap_color or a.native_levels or a.native_bitmap_mask:
        parser.error('Old optional-policy switches require --legacy-debug; current native defaults are tested by verify-spsm-comparison.py')
    binary = a.binary or os.environ.get('BTS_HOST_EXECUTABLE')
    environment = a.environment or os.environ.get('BTS_ENVIRONMENT')
    if not binary or not environment:
        parser.error('Current-policy proof requires --binary and --environment (private build paths); see verify-spsm-comparison.py')
    raise SystemExit(subprocess.call([sys.executable, str(Path(__file__).with_name('verify-spsm-comparison.py')), '--output', str(a.output), '--fixtures', str(a.fixtures), '--format', a.format, '--binary', str(binary), '--environment', str(environment)]))
if a.native_levels and not a.roughness_bitmaps:
    parser.error('--native-levels requires --roughness-bitmaps')
if a.native_bitmap_mask and not a.roughness_bitmaps:
    parser.error('--native-bitmap-mask requires --roughness-bitmaps')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
fixtures = {p.stem: p for p in a.fixtures.resolve().glob('*.spsm')}
assert fixtures, 'No own SPSM test files supplied'
# Import private reflinks/copies, so absence testing never renames artist files.
inputs = a.output/'inputs'
inputs.mkdir(exist_ok=True)
for path in fixtures.values():
    target = inputs/path.name
    if not target.exists():
        copied = subprocess.run(['cp', '--reflink=auto', '--', str(path), str(target)],
                                capture_output=True, check=False)
        if copied.returncode:
            shutil.copy2(path, target)
expected = {name: {'sha256': hashlib.file_digest(p.open('rb'), 'sha256').hexdigest(),
                   'bytes': p.stat().st_size, 'filename': p.name}
            for name, p in fixtures.items()}
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
           LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8', BTS_SPSM_DEMO='1',
           BTS_SPSM_FIXTURES=str(inputs), BTS_SPSM_HIDE_SOURCES='1', BTS_SKEW_DEMO='1',
           BTS_VERIFY_ASYNC_SAVE='1', BTS_PERFORMANCE_PROFILE='1',
           BTS_SESSION_LOG_DIR=str(a.output/'logs'), BTS_RESOURCE_CACHE=str(a.output/'cache'),
           XDG_CONFIG_HOME=str(a.output/'config'), XDG_DATA_HOME=str(a.output/'data'),
           XDG_CACHE_HOME=str(a.output/'cache'), BTS_RECOVERY_DIR=str(a.output/'recovery'))
for key in ('BTS_SPSM_BITMAP_POLICY', 'BTS_SPSM_BITMAP_COLOR_POLICY',
            'BTS_SPSM_LEVELS_POLICY', 'BTS_SPSM_MASK_BITMAP_POLICY'):
    env.pop(key, None)
if a.roughness_bitmaps:
    env['BTS_SPSM_BITMAP_POLICY'] = '1'
if a.bitmap_color:
    env['BTS_SPSM_BITMAP_COLOR_POLICY'] = {
        'linear-rec709': 'native-linear-rec709-rgb-levels-ignore-a',
        'acescg': 'native-acescg-rgb-levels-ignore-a'}[a.bitmap_color]
if a.native_levels:
    env['BTS_SPSM_LEVELS_POLICY'] = '1'
if a.native_bitmap_mask:
    env['BTS_SPSM_MASK_BITMAP_POLICY'] = '1'
try:
    with (a.output/'run.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', str(root/'scripts/host/run-host.sh'),
                        '--no-splash', '--workspace', '--gui-fix-demo', str(a.output),
                        '--gui-fix-mesh', str(root/'fixtures/two_tile_udim.obj'),
                        '--gui-fix-resolution', '128', '--autosave-seconds', '3600',
                        '--library-dir', str(a.output/'library')],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
finally:
    held = a.output/'inputs.held'
    if held.exists() and not inputs.exists():
        held.rename(inputs)
proof = json.loads((a.output/'spsm-import.json').read_text())
captures = {c['stage']: c for c in proof['captures']}
assert len(proof['reports']) >= len(fixtures)
for report in proof['reports']:
    assert 'SHA-256' in report and 'unverified' in report.lower(), report
for i, _ in enumerate(proof['source_files']):
    applied, undone, redone = (captures[f'{stage}-{i}'] for stage in ('applied','undo','redo'))
    def layers(c): return c['document']['texture_sets'][0]['layers']
    assert len(layers(applied)) > len(layers(undone))
    assert layers(applied) == layers(redone)
    assert applied['undo_count'] == undone['undo_count'] + 1
    assert not applied['shader_failures'], applied['shader_failures']
saved, reopened = captures['saved'], captures['reopened']
if a.bitmap_color:
    assert 'used_varnish_wood' in fixtures, 'Color integration requires the actual Wood source'
    color_datasets = {
        'Wood_Worn_base': '/resources/ebd7c2b2e295ba73b056493abb203c0236ab183d.image',
        'Wood_Darken_base': '/resources/1d94222725eb74ea0f4eeea214ad69b835e9a99f.image'}
    for stage in ('saved', 'reopened'):
        layers = captures[stage]['document']['texture_sets'][0]['layers']
        for name in ('Wood_Worn_base', 'Wood_Darken_base'):
            matches = [layer for layer in layers if layer['name'] == name]
            assert len(matches) == 1, (stage, name)
            layer = matches[0]
            mapping = layer['fill_projection']
            if name == 'Wood_Darken_base' and a.roughness_bitmaps and not a.native_levels:
                # The selected Roughness chain has its own Levels. Unknown
                # scalar semantics defer this entire selected leaf atomically;
                # Color's self-contained RGB interpretation cannot enable it.
                assert not layer['visible'] and not mapping['texture'], (stage, name)
                assert not mapping['channel_textures'][1], (stage, name)
                assert not any(effect['type'] == 'linear_color_transform'
                               for effect in layer['effects']), (stage, name)
                continue
            assert layer['channels'][0] and mapping['texture'], (stage, name)
            assert mapping['texture']['raw'] and not mapping['alpha_mask'][0]
            assert mapping['texture']['name'] == color_datasets[name], (stage, name, mapping['texture'])
            color_effects = [effect for effect in layer['effects']
                             if effect.get('channel') == 0]
            assert [effect['type'] for effect in color_effects] == [
                'levels', 'linear_color_transform'], (stage, name, color_effects)
            transform = color_effects[-1]['transform']
            assert transform['input'] == a.bitmap_color and transform['target'] == 'ACEScg'
            assert transform['raw_linear_source']
            if a.roughness_bitmaps:
                assert mapping['channel_textures'][1], (stage, name, 'Roughness source missing')
            else:
                assert not layer['channels'][2] and not mapping['channel_textures'][1], (
                    stage, name, 'Color opted Roughness in')
        ancestors = [layer for layer in layers if layer['name'] == 'Wood_Color_Roughness_base']
        assert len(ancestors) == 1 and ancestors[0]['mask']['enabled']
        assert ancestors[0]['mask']['background'] == 0, 'Original black ancestor changed'
if a.roughness_bitmaps:
    assert 'used_varnish_wood' in fixtures, 'Bitmap integration requires the actual Wood source'
    def wood_bitmap(capture):
        layers = capture['document']['texture_sets'][0]['layers']
        return [layer for layer in layers if layer['name'] == 'Wood_Worn_base'
                and layer['channels'][2]
                and layer['fill_projection']['channel_textures'][1]]
    assert wood_bitmap(captures['saved']), 'No supported native Wood Roughness texture was applied'
    assert wood_bitmap(captures['reopened']), 'Packed Open lost the native Roughness texture'
    if a.native_levels:
        for stage in ('saved', 'reopened'):
            darken = [layer for layer in captures[stage]['document']['texture_sets'][0]['layers']
                      if layer['name'] == 'Wood_Darken_base' and layer['channels'][2]
                      and layer['fill_projection']['channel_textures'][1]
                      and any(effect['type'] == 'levels' and effect['channel'] == 2
                              for effect in layer['effects'])]
            assert darken, (stage, 'Bitmap + native scalar Levels binding missing')
    if a.native_bitmap_mask:
        for stage in ('saved', 'reopened'):
            extra = [layer for layer in captures[stage]['document']['texture_sets'][0]['layers']
                     if layer['name'] == 'Wood_base_XtraRoughness' and layer['channels'][2]
                     and layer['fill_projection']['channel_textures'][1]]
            assert len(extra) == 1, (stage, 'Atomic content/bitmap-mask binding missing')
            mask = extra[0]['mask']
            assert mask['enabled'], (stage, 'Bitmap mask unexpectedly disabled')
            effects = mask['effects']
            assert [effect['type'] for effect in effects] == ['fill', 'levels'], (stage, effects)
            assert effects[0]['mapping']['texture'], (stage, 'Mask bitmap resource missing')
            assert not effects[0]['mapping']['use_alpha'], (stage, 'Raw-R mask must ignore alpha')
            ancestors = [layer for layer in captures[stage]['document']['texture_sets'][0]['layers']
                         if layer['name'] == 'Wood_Color_Roughness_base']
            assert len(ancestors) == 1 and ancestors[0]['mask']['enabled']
            assert ancestors[0]['mask']['background'] == 0, 'Original black ancestor changed'


assert saved['document']['texture_sets'] == reopened['document']['texture_sets']
assert not reopened['shader_failures']
assert proof['original_inputs_hidden_during_open']
assert proof['original_source_cache_removed_before_open']
for stage in ('saved','reopened'):
    resources = [r for r in captures[stage]['resources'] if r['original_spsm']]
    assert len(resources) == len(fixtures)
    for resource in resources:
        source = expected[resource['name']]
        for key in ('bytes','filename','sha256'):
            assert resource[key] == source[key], (stage, resource['name'], key)
        policies = {tag for tag in resource.get('tags', []) if tag.startswith('spsm:policy:')}
        expected_policies = {'spsm:policy:uniform-input=Linear Rec.709'}
        if a.roughness_bitmaps or a.bitmap_color:
            expected_policies.update({
                'spsm:policy:bitmap-roughness=' + ('raw-r-ignore-a' if a.roughness_bitmaps else 'unknown'),
                'spsm:policy:bitmap-color=' + env.get('BTS_SPSM_BITMAP_COLOR_POLICY', 'unknown'),
                'spsm:policy:bitmap-bytes=little-endian',
                'spsm:policy:bitmap-rows=first-row-v0',
                'spsm:policy:bitmap-alpha=data-components',
                'spsm:policy:bitmap-uv=projection0-centered-repeat-factors',
                'spsm:policy:bitmap-sampling=hq-repeat'})
        if a.native_levels:
            expected_policies.add('spsm:policy:levels=native-scalar')
        if a.native_bitmap_mask:
            expected_policies.add('spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a')
        assert policies == expected_policies, (stage, resource['name'], policies, expected_policies)
oracle_scopes = {name: capture.get('spsm_oracle_scope') for name, capture in captures.items()
                 if name.startswith('applied-') or name == 'reopened'}
assert oracle_scopes and all(isinstance(scope, dict) and scope.get('status') in ('passed', 'unavailable')
                             for scope in oracle_scopes.values()), 'Missing explicit per-capture oracle scope'
for name, scope in oracle_scopes.items():
    if scope['status'] == 'unavailable':
        assert scope.get('reason'), (name, 'Unavailable spatial reference requires a reason')
(a.output/'verified.json').write_text(json.dumps({
    'direct_original_source': True, 'artist_new_import_and_shelf': True,
    'partial_reports_explicit': True, 'one_undo_per_apply': True,
    'undo_redo': True, 'packed_reopen_original_hashes': True,
    'packed_reopen_without_imported_files_or_source_cache': True,
    'gpu_oracle_diagnostic_invoked': True, 'gpu_oracle_scopes': oracle_scopes,
    'whole_material_cpu_parity_verified': all(scope['status'] == 'passed' for scope in oracle_scopes.values()), 'explicit_roughness_bitmap_policy': a.roughness_bitmaps,
    'explicit_named_linear_color': a.bitmap_color or 'unknown',
    'explicit_native_scalar_levels': a.native_levels,
    'explicit_native_bitmap_mask': a.native_bitmap_mask, 'appearance_verified': False,
    'source_files': expected, 'captures': proof['captures']}, indent=2)+'\n')
print('Direct partial SPSM GUI import/history/packed Open PASS; full appearance remains unverified')
