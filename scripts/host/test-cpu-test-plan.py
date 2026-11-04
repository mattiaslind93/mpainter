"""CPU-only selection/generation regressions. Never starts an app or renderer."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from cpu_test_plan import cpu_test_binaries

ROOT = Path(__file__).resolve().parents[2]


class CpuPlanTest(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(os.environ.get('TMPDIR', ROOT.parent / 'test-runs'))
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='cpu-plan-', dir=temporary_root)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def manifest(self, names, config='Release', version=1):
        path = self.directory / f'bts-cpu-tests-{config}.json'
        path.write_text(json.dumps({'version': version, 'configuration': config, 'binaries': names}))
        return path

    def test_unlisted_gpu_executable_never_becomes_cpu_test(self):
        (self.directory / 'bin').mkdir()
        (self.directory / 'bin/bts-unreviewed-gpu-tests').touch()
        self.manifest(['bts-fixture-tests'])
        self.assertEqual(cpu_test_binaries(self.directory),
                         [self.directory / 'bin/bts-fixture-tests'])

    def test_missing_or_ambiguous_manifest_fails_closed(self):
        with self.assertRaises(ValueError):
            cpu_test_binaries(self.directory)
        self.manifest(['bts-fixture-tests'])
        self.manifest(['bts-debug-tests'], config='Debug')
        with self.assertRaises(ValueError):
            cpu_test_binaries(self.directory)
        self.assertEqual(cpu_test_binaries(self.directory, 'Debug')[0].name, 'bts-debug-tests')

    def test_invalid_identity_or_version_is_rejected(self):
        for names, version in [(['../bts-escape-tests'], 1), (['bts-host'], 1),
                               (['bts-fixture-tests', 'bts-fixture-tests'], 1),
                               ([], 1), (['bts-fixture-tests'], 2)]:
            self.manifest(names, version=version)
            with self.assertRaises(ValueError):
                cpu_test_binaries(self.directory)

    def test_plan_only_never_runs_executables_and_forces_gpu_ban(self):
        self.manifest(['bts-fixture-tests', 'bts-gpu-program-warmup-tests'])
        bin_dir = self.directory / 'bin'
        bin_dir.mkdir()
        sentinel = self.directory / 'unexpected-process'
        for name in ('bts-host', 'bts-fixture-tests', 'bts-gpu-program-warmup-tests'):
            executable = bin_dir / name
            executable.write_text(f'#!/bin/sh\ntouch "{sentinel}"\n')
            executable.chmod(0o755)
        output = self.directory / 'plan'
        env = dict(os.environ, BTS_NO_BACKGROUND_GPU='0', QT_QPA_OFFSCREEN_NO_GLX='0')
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/host/verify-cpu.py'),
                                 '--build-dir', str(self.directory), '--output', str(output),
                                 '--plan-only'], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        plan = json.loads((output / 'plan.json').read_text())
        self.assertEqual(plan['BTS_NO_BACKGROUND_GPU'], '1')
        self.assertEqual(plan['QT_QPA_OFFSCREEN_NO_GLX'], '1')
        self.assertFalse(sentinel.exists())
        for _, command in plan['commands']:
            self.assertNotIn('verify-mask-fill-ui.py', [Path(arg).name for arg in command])
            if Path(command[0]).name == 'bts-host':
                self.assertTrue(command[1].startswith('--cpu-'), command)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for metadata generation')
    def test_actual_cmake_group_generates_only_explicit_targets(self):
        # Configure the production metadata block against two tiny targets.
        # Neither target is compiled or executed.
        source = (ROOT / 'native/host/CMakeLists.txt').read_text()
        end = 'cmake_language(DEFER CALL bts_write_cpu_manifest)'
        block = source[source.index('function(bts_write_cpu_manifest)'):source.index(end) + len(end)]
        (self.directory / 'dummy.cc').write_text('int main(){return 0;}\n')
        (self.directory / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(CpuPlan LANGUAGES CXX)\n'
            'add_executable(bts-fixture-tests EXCLUDE_FROM_ALL dummy.cc)\n'
            'add_executable(bts-deferred-tests EXCLUDE_FROM_ALL dummy.cc)\n'
            'add_executable(bts-unreviewed-gpu-tests EXCLUDE_FROM_ALL dummy.cc)\n'
            'add_custom_target(bts-source-check)\n'
            'add_custom_target(bts-cpu-tests)\n'
            'add_dependencies(bts-cpu-tests bts-fixture-tests bts-source-check)\n'
            'function(register_later)\n'
            'add_dependencies(bts-cpu-tests bts-deferred-tests)\n'
            'endfunction()\ncmake_language(DEFER CALL register_later)\n' + block)
        build = self.directory / 'build'
        result = subprocess.run(['cmake', '-S', str(self.directory), '-B', str(build),
                                 '-DCMAKE_BUILD_TYPE=Release'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        suffix = '.exe' if os.name == 'nt' else ''
        expected = [name + suffix for name in ('bts-deferred-tests', 'bts-fixture-tests')]
        self.assertEqual([p.name for p in cpu_test_binaries(build)], expected)
        for name in expected:
            self.assertFalse((build / name).exists())


if __name__ == '__main__':
    unittest.main()
