# SPDX-License-Identifier: GPL-3.0-or-later
"""The single six-second captured-revision follow-up; no production overrides."""
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time

import cycles_integrated_snapshot as integrated
from cycles_burst_snapshot import PROFILE, COMPILED_TEST_NAMES, overlay as burst_overlay, validate_sources
from cycles_live_manifest import fresh_runtime

ACTOR_SHA = {
    'native/tests/cycles_live_burst_main.cc': '27ffe28f10af60e75e01cc5843db8aabc106be7e81e6ad94e0c60f933dd9f92c',
    'native/tests/cycles_live_burst_test.cc': '2b9401d8ef5249e86d7658b0c5e18af8edd5ddfd670bf8312db2df34338fa1df',
    'scripts/host/check-cycles-live-burst.py': '62928022a8de88a4adc4f4f148d625bc4e318b48028753239b85e580475bf5e5',
    'scripts/host/cycles_burst_snapshot.py': '7cc44398ec059c2a67a77c5aff3c2b6f623ae4f307a47425d2a4fd088c2f80dd',
}
# Preserve old evidence identity separately. Current actor admits only the
# reviewed memory-refusal removal and corresponding source-verifier update.
HISTORICAL_ACTOR_SHA = ACTOR_SHA.copy()
ACTOR_SHA = {**ACTOR_SHA,
    'native/tests/cycles_live_burst_test.cc': '9f9daf257cf4d250a43d5434b1784a25c33dac42a1a4a1845de850bd3040ac4b',
    'scripts/host/cycles_burst_snapshot.py': '1e8160331d4700d7b85e083e4d1a96a6e2393cc8ac3197c0998a6b88e1ab2de8',
}
# Re-pinned 2026-11-09 with the owner's approval, when GuiFixDemo.cc/.hh moved to
# native/harness: the burst main's only change is that include (was 27ffe28f...), and
# cycles_burst_snapshot.py only re-pins cycles_live_udim_artist_main.cc (was 1e816033...).
ACTOR_SHA = {**ACTOR_SHA,
    'native/tests/cycles_live_burst_main.cc': 'acb9b7d30cc2cf14a514cceb030817bec4026e6aabdf7587799f1a74bac5f852',
    'scripts/host/cycles_burst_snapshot.py': 'd7fdf27b7018a7fa8af7687a953eacd0a58dd61297841aca1cd008454b751e14',
}
# Re-pinned 2026-11-11 with the owner's approval: the only source change is the ../host/...
# include path after the seven-file move (../host/main.cc → ../host/app/main.cc in the burst
# main, was acb9b7d3...; ../host/GpuProjectReadback.hh → ../host/blender/... and
# ../host/PropertyLatency.hh → ../host/diagnostics/... in the burst test, was 9f9daf25...).
# cycles_burst_snapshot.py only re-pins the three udim fixtures (was d7fdf27b...).
ACTOR_SHA = {**ACTOR_SHA,
    'native/tests/cycles_live_burst_main.cc': 'ebf2ff24a2fdbce10bfbf059d0e61504307e3afc02a56c55e7709242c0e8469b',
    'native/tests/cycles_live_burst_test.cc': '7d21683feeb389541aecabc96b357670430fe80edc516623fa936effe5f24d72',
    'scripts/host/cycles_burst_snapshot.py': '52afb8b66a8c47bda604d22de355d6c671e596afdd4eb6f80eac3537c3f7adde',
}

# Historical evidence is SHA-pinned as historical, never a production parent
# for the new integration and never passed a forged new snapshot identity.
HISTORY = {
    'ba-integrated-cycles-complete/run-manifest.json': '55172c908f0ec122acdfb66d849a9f5ae13338e014958ec8063957550418a1c3',
    'ba-integrated-cycles-controlled5/run-manifest.json': '859dc5f30e7ed6aae45f3865b5e4982b3ad7307d43e18012268eb25798eeb7a5',
    'ba-live-burst-final/runs/attempt-xaocgo4f/cycles-live-burst.json': '47d5b90501f1780bd6bef5a50a25dfd7175ca1afc00c3db13c608f131cdd1453',
    'ba-live-burst-final/runs/attempt-xaocgo4f/inconclusive-summary.json': 'c11f825c42082dec88a99ae43bdd02cc6272cf8766ff68f087ae9ddfe6aafbd9',
    'ba-live-burst-final/runs/attempt-xaocgo4f/run.log': 'c6e32ae7a688d8c0bb0d9ca087bcddc171d845251ff53c3ff008a201f88cf6de',
    'ba-live-burst-final/runs/attempt-xaocgo4f/provenance/run-manifest.json': 'e4a6ac574a22d1a703fff7d70dcc4fe75d3d10883c73f70b9728eafbbc22591f',
    'ba-live-burst-six/run-manifest.json': 'c730af4fb6428f1d2eae1deca8e1f4c7bfa73b7f973e740e600b84c39f0e645b',
    'ba-live-burst-six/build.json': '2c5c636d0bd6e7152e425f8d76997d40c4178a4aa19ddc65432ce9dc3f7e6ef7',
    'ba-live-burst-six/runs/attempt-7ffkc7um/cycles-live-burst.json': 'a162974a26eff5a561ee2cb93815473e13f153d7e5f2b713cb2ce1bdcbfcddd0',
    'ba-live-burst-six/runs/attempt-7ffkc7um/burst-assessment.json': '33656dbe1b946398281bdd2da3bb61002f111a5c97e2ad5575f887ea0fe14e34',
    'ba-live-burst-six/runs/attempt-7ffkc7um/run.log': '32066ac204021e9ac08cda49a3601c6374252cb3d79713cad655eb09d483c616',
    'ba-captured-revisions-native-first-sdk/fixed-run-manifest.json': '4c6896e436e9ddd2f8175c2538b168cb8a32f611247d359f6b33356421114a5e',
    'ba-captured-revisions-native-first-sdk/build.json': '836f20ef08c3531c632657f57d94a3e4edd148e8dd0158ee6c73d4b6bff72f65',
    'ba-captured-revisions-native-first-sdk/fixed-burst-gduwqjnn/fixed-summary.json': '7df4a36f7a111ea7e4cbb9633c36f3f97dcd39d0ba86ecb41d655d10f115b094',
    'ba-captured-revisions-native-first-sdk/fixed-burst-gduwqjnn/cycles-live-burst.json': '7137808281ad9e1fe0e52d34da42f1d184f6bbe751cb4fc62c246281c78802ce',
    'ba-captured-revisions-native-first-sdk/fixed-burst-gduwqjnn/run.log': 'cb66ef3e1818d72852cc0c53663b2f050d5d05654842f4a1e5600e3fca3af956',
    'ba-captured-revisions-native-first-sdk/fixed-burst-gduwqjnn/burst-assessment.json': 'b6570165389566fa9c2f921bb4c7c8480d5e34ae989c203c59a4dd918f9a2aba',
}
sha = integrated.sha256

def require_hash(path, expected):
    if sha(path) != expected:
        raise ValueError('Changed admitted input: ' + str(path))

def actor_identity(repo):
    validate_sources(repo)  # Exact final fixture, sole friend and callback ABI.
    for name, digest in ACTOR_SHA.items():
        require_hash(repo / name, digest)
    return ACTOR_SHA.copy()

def history_identity(root):
    root = root.resolve()
    result = []
    for name, digest in HISTORY.items():
        path = (root / name).resolve()
        if not path.is_relative_to(root):
            raise ValueError('Historical evidence escapes its root')
        require_hash(path, digest)
        result.append({'path': str(path), 'sha256': digest, 'role': 'historical-only'})
    return result

def output_path(out, snapshot, build_root):
    out, snapshot, build_root = (p.resolve() for p in (out, snapshot, build_root))
    if (not out.is_relative_to(build_root) or out == build_root or
            out.is_relative_to(build_root / 'host') or out.is_relative_to(snapshot) or snapshot.is_relative_to(out)):
        raise ValueError('Output must be private, outside artist and snapshot storage')
    return out

def runner_identity(repo):
    result = integrated.runner_identities(repo, burst=True)
    for name in ('cycles_captured_verifier.py', 'verify-cycles-captured-revisions.py'):
        result['scripts/host/' + name] = sha(repo / 'scripts/host' / name)
    for name in ('run-host.sh', 'gpu-env.sh', 'session_logging.py'):
        result['scripts/host/' + name] = sha(repo / 'scripts/host' / name)
    result['fixtures/two_tile_udim.obj'] = sha(repo / 'native/tests/fixtures/two_tile_udim.obj')
    return result

def validate_record(record):
    if (record.get('kind') != 'integrated-captured-revision-followup' or record.get('profile') != PROFILE or
            record.get('production_units_recompiled') != [] or record.get('fork_patches_reapplied') is not False or
            [u.get('source') for u in record.get('compiled_test_units', [])] != ['native/tests/' + n for n in COMPILED_TEST_NAMES] or
            len(record.get('links', [])) != 1 or record.get('actor_identity') != ACTOR_SHA or
            record.get('positive_minimum_publications') != 2):
        raise ValueError('Not the exact two-TU captured-revision follow-up')
    if any(item.get('role') != 'historical-only' for item in record.get('historical_evidence', [])):
        raise ValueError('Historical evidence is not a production parent')
    for unit in record['compiled_test_units']:
        if unit.get('source_sha256') != ACTOR_SHA[unit['source']]:
            raise ValueError('Changed compiled actor source')
        if controlled_define(unit.get('command', [])):
            raise ValueError('Controlled observer enabled')

def controlled_define(args):
    return any(value.startswith('-DBTS_BA_CONTROLLED_PROFILE_TEST') or
               (value == '-D' and i + 1 < len(args) and args[i + 1].split('=', 1)[0] == 'BTS_BA_CONTROLLED_PROFILE_TEST')
               for i, value in enumerate(args))

def frozen_command(template, source, obj, snapshot, context, native_root, admitted):
    args = [v.replace(native_root, str(context / 'native'))
            .replace(admitted['blender_root'], str(snapshot / 'integration-blender'))
            .replace(admitted['source_build'], str(snapshot / 'integration-generated')) for v in template]
    args[args.index('-c') + 1], args[args.index('-o') + 1] = str(source), str(obj)
    for flag, value in (('-MF', str(obj) + '.d'), ('-MT', str(obj))):
        if flag in args:
            args[args.index(flag) + 1] = value
    return args

def build(snapshot, repo, out, history_root, jobs):
    if jobs not in (1, 2):
        raise ValueError('At most two compiler jobs')
    actor = actor_identity(repo); history = history_identity(history_root); runners = runner_identity(repo)
    overlay = burst_overlay(snapshot, repo)
    admitted = integrated.validate(snapshot, repo, overlay, PROFILE)
    out.mkdir(exist_ok=False)
    context = out / 'burst-source'
    shutil.copytree(snapshot / 'integration-source', context)
    for relative in overlay:
        dest = context / relative; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / relative, dest)
    sources = integrated.validate_overlay_copy(out, admitted, overlay, burst=True)
    commands = json.loads((snapshot / 'host-commands.json').read_text())
    templates = [shlex.split(c) for c in commands if ' -c ' in c]
    template = next(c for c in templates if c[c.index('-c') + 1].endswith('/native/host/app/main.cc'))
    native_root = template[template.index('-c') + 1].split('/native/', 1)[0] + '/native'
    env = dict(os.environ, CCACHE_DIR=str(repo.parent / 'build/ccache'))
    def call(args, label):
        with (out / (label + '.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *args], cwd=snapshot, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    discovery = {}
    for name in COMPILED_TEST_NAMES:
        args = [v.replace(native_root, str(context / 'native')) for v in template]
        args[args.index('-c') + 1] = str(context / 'native/tests' / name)
        discovery[name] = args + ['-I' + str(context / 'native/host'), '-g0']
    supplement = integrated.freeze_test_headers(snapshot, out, admitted, discovery, call, context, sources)
    allowed = {str(context / p): v for p, v in sources.items()}
    for field, folder in (('blender_source_sha256', snapshot / 'integration-blender'), ('generated_sha256', snapshot / 'integration-generated')):
        allowed.update({str(folder / p): v for p, v in admitted[field].items()})
    for p, v in supplement['production_context_sha256'].items(): allowed[str(out / 'test-headers/blender' / p)] = v
    for p, v in supplement['headers'].items(): allowed[str(out / 'test-headers/blender' / p)] = v['sha256']
    allowed.update(supplement['external_sha256'])
    (out / 'objects').mkdir(); (out / 'bin').mkdir()
    units = []
    for name in COMPILED_TEST_NAMES:
        path = context / 'native/tests' / name; obj = out / 'objects' / (Path(name).stem + '.o')
        args = frozen_command(template, path, obj, snapshot, context, native_root, admitted)
        args = [v.replace(str(snapshot / 'integration-blender'), str(out / 'test-headers/blender')) for v in args]
        args += ['-I' + str(context / 'native/host'), '-g0']
        if controlled_define(args):
            raise ValueError('The controlled observer must remain disabled')
        dep = out / (name + '.frozen.d'); call(integrated.dependency_scan(args, dep), name + '-closure')
        for dependency in integrated.make_dependencies(dep):
            dependency = integrated.dependency_path(str(dependency), snapshot)
            if str(dependency) not in allowed: raise ValueError('Unadmitted frozen header: ' + str(dependency))
            require_hash(dependency, allowed[str(dependency)])
        units.append({'source': 'native/tests/' + name, 'source_sha256': sha(path), 'object': str(obj), 'command': args, 'closure_sha256': sha(dep)})
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for job in [pool.submit(call, u['command'], Path(u['source']).stem + '-compile') for u in units]: job.result()
    for unit in units: unit['object_sha256'] = sha(unit['object'])
    args = integrated.link_arguments(commands); main = [v for v in args if v.endswith('/main.cc.o')]
    if len(main) != 1: raise ValueError('Ambiguous production main')
    link = []
    for value in args:
        if value == main[0]: value = units[0]['object']
        elif value in admitted['link_inputs']:
            info = admitted['link_inputs'][value]; value = str(snapshot / info['path']) if info['kind'] == 'frozen' else info['path']
        elif value.startswith('-Wl,--dependency-file='): value = '-Wl,--dependency-file=' + str(out / 'integrated-link.d')
        elif value in admitted['linker_scripts']: value = '-Wl,--version-script=' + str(snapshot / 'integration-blender' / admitted['linker_scripts'][value])
        else: value = value.replace(admitted['source_build'] + '/bin/lib', str(snapshot / 'bin/lib'))
        link.append(value)
    link.insert(next(i for i, value in enumerate(link) if value.endswith('.a')), units[1]['object'])
    binary = out / 'bin/cycles-live-artist'; link[link.index('-o') + 1] = str(binary); call(link, 'artist-link')
    if not (snapshot / 'bin/5.2').is_dir(): raise ValueError('Missing frozen runtime resources')
    (out / 'bin/5.2').symlink_to(snapshot / 'bin/5.2', target_is_directory=True)
    runtime_sources = {}
    for relative in ('scripts/host/run-host.sh', 'scripts/host/gpu-env.sh', 'scripts/host/session_logging.py', 'scripts/host/check-cycles-live-burst.py', 'fixtures/two_tile_udim.obj'):
        dest = out / 'run-source' / relative; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / relative, dest); require_hash(dest, runners[relative]); runtime_sources[str(dest)] = runners[relative]
    environment = repo.parent / 'build/environment-cache/TT_environmentLights_v001.hdr'
    runtime_sources[str(environment)] = sha(environment)
    record = {'kind': 'integrated-captured-revision-followup', 'profile': PROFILE, 'snapshot': str(snapshot), 'snapshot_sha256': sha(snapshot / 'integrated-snapshot.json'),
              'production_source_head': admitted['native_source_head'], 'production_units_recompiled': [], 'fork_patches_reapplied': False, 'actor_identity': actor,
              'positive_minimum_publications': 2, 'historical_evidence': history, 'runner_sha256': runners, 'test_overlay': overlay, 'compiled_test_units': units,
              'links': [{'binary': str(binary), 'sha256': sha(binary), 'command': link}], 'allowed_header_sha256': allowed,
              'runtime_sha256': runtime_sources, 'environment': str(environment), 'native_execution': False}
    validate_record(record)
    integrated.validate(snapshot, repo, overlay, PROFILE); integrated.validate_overlay_copy(out, admitted, overlay, True)
    integrated.validate_test_headers(snapshot, out, supplement)
    if actor_identity(repo) != actor or history_identity(history_root) != history or runner_identity(repo) != runners: raise ValueError('Changed build admission')
    for path, digest in allowed.items(): require_hash(path, digest)
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record

def admission(snapshot, repo, out, history_root, expected_build_sha):
    require_hash(out / 'build.json', expected_build_sha)
    record = json.loads((out / 'build.json').read_text()); validate_record(record)
    if Path(record['snapshot']).resolve() != snapshot or record['snapshot_sha256'] != sha(snapshot / 'integrated-snapshot.json'):
        raise ValueError('Wrong new production snapshot')
    admitted = integrated.validate(snapshot, repo, record['test_overlay'], PROFILE)
    if record['production_source_head'] != admitted['native_source_head']:
        raise ValueError('Wrong production source revision')
    integrated.validate_overlay_copy(out, admitted, record['test_overlay'], True)
    integrated.validate_test_headers(snapshot, out, json.loads((out / 'test-headers.json').read_text()))
    if actor_identity(repo) != record['actor_identity'] or runner_identity(repo) != record['runner_sha256'] or history_identity(history_root) != record['historical_evidence']:
        raise ValueError('Changed source or historical evidence')
    for mapping in (record['allowed_header_sha256'], record['runtime_sha256']):
        for path, digest in mapping.items(): require_hash(path, digest)
    for unit in record['compiled_test_units']: require_hash(unit['object'], unit['object_sha256'])
    require_hash(record['links'][0]['binary'], record['links'][0]['sha256'])
    return record

def require_clean_exit(exit_code, text):
    """Check the entire native log, including teardown after the held interval."""
    if exit_code != 0:
        raise RuntimeError('Actor exited ' + str(exit_code))
    if not re.search(r'\bBTS_SHUTDOWN clean(?:\s|$)', text):
        raise RuntimeError('Native clean shutdown marker missing')
    diagnostic = re.search(r'OpenColorIO\s+Error|Could not find color space|Not freed memory|AddressSanitizer|Shader compilation failed|BTS_GPU_STACK_(?:FALLBACK|ERROR)|BTS_.*CLEANUP_FAILED|BTS_HOST_FAILED|BTS_DRAW_FAILED|color_management\s*\|\s*ERROR', text, re.IGNORECASE)
    if diagnostic:
        raise RuntimeError('Native diagnostic: ' + diagnostic.group(0))

def run(snapshot, repo, out, history_root, expected_build_sha):
    record = admission(snapshot, repo, out, history_root, expected_build_sha)
    runtime = fresh_runtime(out)
    (runtime / 'admission.json').write_text(json.dumps({'build_sha256': expected_build_sha, 'snapshot_sha256': record['snapshot_sha256'],
        'actor_identity': record['actor_identity'], 'historical_evidence': record['historical_evidence'], 'profile': PROFILE}, indent=2) + '\n')
    env = dict(os.environ)
    for key, folder in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs'),('TMPDIR','tmp')):
        (runtime / folder).mkdir(); env[key] = str(runtime / folder)
    for folder in ('cuda','nvidia'): (runtime / 'cache' / folder).mkdir()
    env.update(CUDA_CACHE_PATH=str(runtime/'cache/cuda'), __GL_SHADER_DISK_CACHE_PATH=str(runtime/'cache/nvidia'), BTS_ROOT=str(repo.parent),
        BTS_VERIFY_CYCLES_LIVE='1', BTS_BA_EXPECT_FIXED='1', QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1', BTS_BLEND_MATERIAL_DEMO='1',
        BTS_PROPERTY_PERFORMANCE='1', BTS_PERFORMANCE_PROFILE='1', BTS_HOST_EXECUTABLE=record['links'][0]['binary'],
        QT_PLUGIN_PATH=str(repo.parent/'qt/6.8.3/plugins'), LD_LIBRARY_PATH=str(repo.parent/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib'), BTS_ENVIRONMENT=record['environment'])
    for key in ('BTS_BA_CONTROLLED_PROFILE','BTS_TEST_CONTINUOUS_FRAMES','BTS_BA_TRACE_ONLY'): env.pop(key, None)
    source = out / 'run-source'
    command = ['nice','-n','19','bash',str(source/'scripts/host/run-host.sh'),'--workspace','--no-splash','--gui-fix-demo',str(runtime),
        '--gui-fix-mesh',str(source/'native/tests/fixtures/two_tile_udim.obj'),'--gui-fix-resolution','2048','--library-dir',str(runtime/'shelf'),'--recovery-dir',str(runtime/'recovery')]
    private_keys = ('XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','BTS_RECOVERY_DIR','BTS_RESOURCE_CACHE','BTS_SESSION_LOG_DIR','TMPDIR','CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH','BTS_ROOT','BTS_VERIFY_CYCLES_LIVE','BTS_BA_EXPECT_FIXED','QT_QPA_PLATFORM','BTS_HEADLESS_WORKSPACE','BTS_BLEND_MATERIAL_DEMO','BTS_PROPERTY_PERFORMANCE','BTS_PERFORMANCE_PROFILE','BTS_HOST_EXECUTABLE','QT_PLUGIN_PATH','LD_LIBRARY_PATH','BTS_ENVIRONMENT')
    result = {'profile': PROFILE, 'build_sha256': expected_build_sha, 'command': command, 'cwd': str(source),
        'private_environment': {key: env[key] for key in private_keys},
        'removed_environment_keys': ['BTS_BA_CONTROLLED_PROFILE','BTS_TEST_CONTINUOUS_FRAMES','BTS_BA_TRACE_ONLY'],
        'performance_claim': False, 'rendered_roi_claim': False, 'initial_entry_progress_claim': False}
    started = time.monotonic()
    try:
        with (runtime / 'run.log').open('w') as log:
            process = subprocess.Popen(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
            result['pid'] = process.pid; print('CAPTURED_REVISION_PID', process.pid, 'RUNTIME', runtime, flush=True)
            try: result['exit'] = process.wait(timeout=420)
            except BaseException:
                process.kill(); process.wait(); raise
        # The timestamp logger can finish draining after its native child exits.
        drain_deadline = time.monotonic() + 2
        while True:
            text = (runtime / 'run.log').read_text(errors='replace')
            if result['exit'] or 'BTS_SHUTDOWN clean' in text or time.monotonic() >= drain_deadline:
                break
            time.sleep(.05)
        require_clean_exit(result['exit'], text)
        spec = importlib.util.spec_from_file_location('unchanged_six_classifier', source/'scripts/host/check-cycles-live-burst.py')
        classifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(classifier)
        outcome = classifier.check(runtime); result['assessment'] = outcome
        (runtime / 'burst-assessment.json').write_text(json.dumps(outcome, indent=2) + '\n')
        if outcome['starvation_observed'] or outcome['publications'] < 2:
            raise ValueError('Fewer than two held publications under unchanged oracle')
        admission(snapshot, repo, out, history_root, expected_build_sha); result['status'] = 'PASS'
    except BaseException as error:
        result['status'] = 'FAILED_OR_INCONCLUSIVE'; result['error'] = {'type': type(error).__name__, 'detail': str(error)}
    finally:
        result['monotonic_elapsed_seconds'] = time.monotonic() - started
        result['raw_sha256'] = {name: sha(runtime/name) for name in ('run.log','cycles-live-burst.json','burst-assessment.json') if (runtime/name).exists()}
        (runtime / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    return result['status'] == 'PASS'
