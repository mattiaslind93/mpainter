# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a target-qualified native include closure against frozen host inputs.

The snapshot must include the exact source HEAD, host commands and VALID Ninja
dependency records. No mutable artist object/archive is read or written.
Run CPU/source tests after reserving a build slot; GPU is a separate opt-in phase.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--host-resources', type=Path, required=True,
               help='Stable versioned Blender runtime-data directory parent; not an object/archive source')
p.add_argument('--build-only', action='store_true')
p.add_argument('--run-only', action='store_true')
p.add_argument('--fixtures', type=Path)
p.add_argument('--case', action='append', choices=['bts-host', 'color-cpu-test', 'source-cpu-test', 'ordinary-color-test', 'color-source-test', 'linear-api-test'], help='Repeat a selected CPU regression without relinking unrelated drivers')
a = p.parse_args()
repo = Path(os.environ.get('BTS_REPO', Path(__file__).resolve().parents[2])).resolve()
snap, out = a.host_snapshot.resolve(), a.output.resolve()
if not out.is_relative_to(repo.parent / 'build') or out.is_relative_to(snap) or out.is_relative_to(repo.parent / 'build/host'):
    raise SystemExit('Use separate private build storage')
out.mkdir(parents=True, exist_ok=True)
metadata = json.loads((snap / 'snapshot.json').read_text())
commands = json.loads((snap / 'host-commands.json').read_text())
template = shlex.split(next(c for c in commands if '/main.cc.o -c ' in c))
old = Path(template[template.index('-c') + 1]).parents[2]
env = dict(os.environ, CCACHE_DIR=str(repo.parent / 'build/ccache'))
if not a.run_only:
    subprocess.run(['git', 'merge-base', '--is-ancestor', metadata['source_head'], 'HEAD'], cwd=repo, check=True)
    changed = set(subprocess.check_output(['git', 'diff', '--name-only', metadata['source_head']], cwd=repo, text=True).splitlines())
    changed.update(subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', 'native'], cwd=repo, text=True).splitlines())
    headers = {name for name in changed if name.endswith(('.hh', '.h', '.hpp'))}
    production_hashes = {name: hashlib.sha256((repo / name).read_bytes()).hexdigest()
                         for name in changed if name.startswith('native/host/') and name.endswith(('.cc', '.hh', '.h', '.hpp'))}
    units = {}
    for command in commands:
        args = shlex.split(command)
        if '-c' not in args or '-o' not in args:
            continue
        source = Path(args[args.index('-c') + 1])
        if source.is_relative_to(old / 'native'):
            units[args[args.index('-o') + 1]] = args
    declared = set(json.loads((snap / 'native-dependency-objects.json').read_text()))
    if declared != set(units):
        raise SystemExit('Frozen dependency inventory does not cover exact native compile graph')
    dependencies, current = {}, None
    for line in (snap / 'native-deps.txt').read_text().splitlines():
        if ': #deps ' in line:
            current = line.split(': #deps ', 1)[0]
            if '(VALID)' not in line:
                raise SystemExit('Invalid frozen native dependency record: ' + current)
            dependencies[current] = set()
        elif current is not None and line.startswith('    ') and '/native/' in line:
            dependencies[current].add('native/' + line.strip().split('/native/', 1)[1])
    if not declared.issubset(dependencies):
        raise SystemExit('Missing frozen native dependency coverage')
    selected = {}
    for obj, args in units.items():
        source = str(Path(args[args.index('-c') + 1]).relative_to(old))
        if source in changed or headers & dependencies[obj]:
            selected[obj] = args
    new_embedded, existing_embedded = {}, {}
    for source in ['native/tests/ordinary_color_source_test.cc', 'native/tests/spsm_bitmap_color_test.cc']:
        found = [key for key, args in units.items() if Path(args[args.index('-c') + 1]) == old / source]
        if found:
            existing_embedded.update({key: source for key in found})
            continue
        args = template.copy()
        args[args.index('-c') + 1] = str(old / source)
        target = template[template.index('-o') + 1].rsplit('/native/', 1)[0] + '/' + source
        target += '.o'
        args[args.index('-o') + 1] = target
        args.append('-DBTS_EMBED_SOURCE_REGRESSIONS=1')
        selected[target] = args
        new_embedded[target] = source
    extras = ['native/tests/spsm_bitmap_color_test.cc', 'native/tests/spsm_source_test.cc', 'native/tests/ordinary_color_source_test.cc', 'native/tests/spsm_source_color_test.cc', 'native/tests/linear_color_transform_test.cc']
    for label, source in zip(['color-cpu-test', 'source-cpu-test', 'ordinary-color-test', 'color-source-test', 'linear-api-test'], extras):
        if a.case and label not in a.case:
            continue
        args = template.copy()
        args[args.index('-c') + 1] = str(old / source)
        args[args.index('-o') + 1] = '__tests__/' + Path(source).name + '.o'
        selected[args[args.index('-o') + 1]] = args
    (out / 'closure.json').write_text(json.dumps({'snapshot_source': metadata['source_head'], 'changed_files': sorted(changed), 'changed_headers': sorted(headers), 'covered_native_units': len(units), 'selected_objects': sorted(selected)}, indent=2) + '\n')

    def compile_item(item):
        original, args = item
        args = [x.replace(str(old), str(repo)) for x in args]
        obj = out / 'objects' / original
        obj.parent.mkdir(parents=True, exist_ok=True)
        args[args.index('-o') + 1] = str(obj)
        for option, value in [('-MF', str(obj) + '.d'), ('-MT', str(obj))]:
            if option in args:
                args[args.index(option) + 1] = value
        if original.startswith('__tests__/'):
            args.append('-UNDEBUG')
        print('Compile', original, flush=True)
        with Path(str(obj) + '.log').open('w') as log:
            subprocess.run(['nice', '-n', '19', *args], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        return original, str(obj)

    with ThreadPoolExecutor(max_workers=2) as pool:
        replacements = dict(pool.map(compile_item, selected.items()))
    if any(hashlib.sha256((repo / name).read_bytes()).hexdigest() != digest for name, digest in production_hashes.items()):
        raise SystemExit('Production sources changed during native closure compile; refresh before testing')
    (out / 'objects.json').write_text(json.dumps(replacements, indent=2) + '\n')
    (out / 'production-source-hashes.json').write_text(json.dumps({'snapshot_head': metadata['source_head'], 'sources': production_hashes}, indent=2) + '\n')
    archives = {}
    for command in commands:
        if 'ar qc' not in command or 'libbts-' not in command:
            continue
        args = shlex.split(next(part for part in command.split(' && ') if 'ar qc' in part))
        archive, members = args[2], args[3:]
        if not any(member in replacements for member in members):
            continue
        target = out / 'archives' / Path(archive).name
        target.parent.mkdir(exist_ok=True)
        fresh = [Path(replacements[member]) if member in replacements else snap / member for member in members]
        if not all(path.is_file() for path in fresh):
            raise SystemExit('Archive has missing frozen/fresh members')
        target.unlink(missing_ok=True)
        subprocess.run(['nice', '-n', '19', args[0], 'qc', str(target), *map(str, fresh)], cwd=snap, check=True)
        subprocess.run(['nice', '-n', '19', 'ranlib', str(target)], check=True)
        entries = []
        for member in fresh:
            digest = hashlib.sha256(member.read_bytes()).hexdigest()
            packed = subprocess.check_output(['nice', '-n', '19', args[0], 'p', str(target), member.name])
            if hashlib.sha256(packed).hexdigest() != digest:
                raise SystemExit('Archive membership mismatch: ' + str(member))
            entries.append({'member': str(member), 'sha256': digest})
        replacements[archive] = str(target)
        archives[str(target)] = entries
    (out / 'archives.json').write_text(json.dumps(archives, indent=2) + '\n')
    base_link = shlex.split(next(part for part in commands[-1].split(' && ') if ' -o ' in part))
    main = template[template.index('-o') + 1]
    manifests = {}
    for label, driver in [('bts-host', None), ('color-cpu-test', extras[0]), ('source-cpu-test', extras[1]), ('ordinary-color-test', extras[2]), ('color-source-test', extras[3]), ('linear-api-test', extras[4])]:
        if a.case and label not in a.case:
            continue
        override = dict(replacements)
        if driver:
            override[main] = replacements['__tests__/' + Path(driver).name + '.o']
        link = [override.get(x, x) for x in base_link]
        link = [x for x in link if x not in {override.get(key, key) for key, source in existing_embedded.items() if source == driver}]
        insert = next(i for i, x in enumerate(link) if x.endswith('.o'))
        link[insert:insert] = [replacements[key] for key, source in new_embedded.items() if source != driver]
        link[link.index('-o') + 1] = str(out / label)
        link = [('-Wl,--dependency-file=' + str(out / (label + '.d'))) if x.startswith('-Wl,--dependency-file=') else x.replace(metadata['source_build'] + '/bin/lib', str(snap / 'bin/lib')) for x in link]
        inputs = []
        for token in link:
            if not token.endswith(('.o', '.a')):
                continue
            path = Path(token)
            if not path.is_absolute():
                path = snap / path
            path = path.resolve()
            if not path.is_file() or path.is_relative_to(Path(metadata['source_build']).resolve()):
                raise SystemExit('Missing/mutable link input: ' + str(path))
            inputs.append(str(path))
        manifests[label] = inputs
        with (out / (label + '.link.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *link], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / 'link-inputs.json').write_text(json.dumps(manifests, indent=2) + '\n')
    for resource in a.host_resources.resolve().iterdir():
        if resource.is_dir() and resource.name[0].isdigit() and not (out / resource.name).exists():
            (out / resource.name).symlink_to(resource, target_is_directory=True)
if a.build_only:
    raise SystemExit(0)
proof = json.loads((out / 'production-source-hashes.json').read_text())
if proof['snapshot_head'] != metadata['source_head'] or any(hashlib.sha256((repo / name).read_bytes()).hexdigest() != digest for name, digest in proof['sources'].items()):
    raise SystemExit('Native overlay production source hashes differ; no test process started')
env.update(QT_QPA_PLATFORM='offscreen', LD_LIBRARY_PATH=str(snap / 'bin/lib') + ':' + env.get('LD_LIBRARY_PATH', ''), BTS_HEADLESS_WORKSPACE='1')
(out / 'tmp').mkdir(exist_ok=True)
env['TMPDIR'] = str(out / 'tmp')
env['QT_PLUGIN_PATH'] = str(repo.parent / 'qt/6.8.3/plugins')
configs = list(a.host_resources.resolve().glob('*/datafiles/colormanagement/config.ocio'))
if not configs:
    raise SystemExit('Missing stable OCIO configuration for named Source policy tests')
env['OCIO'] = str(configs[0])
for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'), ('BTS_SESSION_LOG_DIR', 'logs')]:
    env[key] = str(out / name)
jobs = [('ordinary-color-test', [str(out)]), ('linear-api-test', [])]
jobs.append(('bts-host', ['--no-splash', '--cpu-native-material-test', str(out / 'standard-cpu-mode')]))
if a.fixtures:
    jobs += [('source-cpu-test', [str(a.fixtures.resolve())]), ('color-cpu-test', [str(a.fixtures.resolve()), str(out / 'actual-bindings.json')]), ('color-source-test', [str(a.fixtures.resolve())])]
else:
    jobs += [('color-cpu-test', [])]
for binary, arguments in jobs:
    if a.case and binary not in a.case:
        continue
    run_env = dict(env)
    if binary == 'bts-host':
        run_env.pop('BTS_NATIVE_MATERIAL_CASES', None)
    with (out / (binary + '.log')).open('w') as log:
        subprocess.run(['nice', '-n', '19', str(out / binary), *arguments], cwd=repo, env=run_env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
print('SPSM ordered RGB lowering/controller CPU suites PASS; no GPU contexts')
