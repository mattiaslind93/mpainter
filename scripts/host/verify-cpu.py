# SPDX-License-Identifier: GPL-3.0-or-later
"""Run native CPU assertions and real headless image-export regression (no OpenGL)."""
import argparse,json,os,subprocess,sys
from pathlib import Path
from cpu_test_plan import cpu_test_binaries
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--export-only',action='store_true');p.add_argument('--config');p.add_argument('--plan-only',action='store_true');a=p.parse_args();a.build_dir=a.build_dir.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
(a.output/'tmp').mkdir(exist_ok=True)
env=dict(os.environ,TMPDIR=str(a.output.resolve()/'tmp'),XDG_CACHE_HOME=str(a.output.resolve()/'cache'),QT_QPA_PLATFORM='offscreen',QT_QPA_OFFSCREEN_NO_GLX='1',BTS_NO_BACKGROUND_GPU='1',XDG_CONFIG_HOME=str(a.output.resolve()/'config'),XDG_DATA_HOME=str(a.output.resolve()/'data'),BTS_RECOVERY_DIR=str(a.output.resolve()/'recovery'),BTS_RESOURCE_CACHE=str(a.output.resolve()/'resource-cache'))
env.pop('WAYLAND_DISPLAY',None)
bts_root=Path(env.get('BTS_ROOT',a.build_dir.parent.parent));sdk=bts_root/'qt/6.8.3'
if sdk.is_dir():env.update(QT_PLUGIN_PATH=str(sdk/'plugins'),LD_LIBRARY_PATH=str(sdk/'lib')+(':'+env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else ''))
env['BTS_ROOT']=str(bts_root)
# Build-tree executables are outside the packaged runtime resource directory.
materialx=a.build_dir.parent/'runtime/lib/materialx'
if (materialx/'libraries').is_dir():env.setdefault('BTS_MATERIALX_BASE',str(materialx))
results=[]
commands=[] if a.export_only else [(binary.name,[str(binary)]+([str(bts_root/'build')] if binary.name=='bts-asset-tests' else [str((a.output/'extent-recovery').resolve())] if binary.name=='bts-texture-extent-tests' else ['--write',str((a.output/'file-browser').resolve())] if binary.name=='bts-file-dialog-tests' else [])) for binary in cpu_test_binaries(a.build_dir,a.config)]
# The warmup regression is mandatory even when a stale build lacks its binary.
if not a.export_only and not any(name=='bts-gpu-program-warmup-tests' for name,_ in commands):
 raise SystemExit('CMake CPU group is missing the mandatory CPU warmup regression')
if not a.export_only:
 file_dialog=str(a.build_dir/'bin/bts-file-dialog-tests')
 for test,mode,folder in (('bts-file-dialog-persistence','--read','file-browser'),('bts-file-dialog-state-write','--state-write','file-browser-state'),('bts-file-dialog-state-read','--state-read','file-browser-state'),('bts-file-dialog-plain-qt','--plain-qt','file-browser-plain')):
  commands.append((test,[file_dialog,mode,str((a.output/folder).resolve())]))
if not a.export_only:
 for script in ('verify-gpu-only-paths.py','verify-save-scheduling.py','verify-endurance-guard.py','test-session-guard.py','test-cpu-test-plan.py','verify-smart-mask-paths.py','verify-smart-material-paths.py'):
  commands.append((script,[sys.executable,str(Path(__file__).with_name(script))]))
 commands.append(('bts-cycles-live-manifest',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_live_manifest_test.py')]))
 commands.append(('bts-cycles-integrated-snapshot',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_integrated_snapshot_test.py')]))
 commands.append(('bts-cycles-captured-revision-admission',[sys.executable,str(Path(__file__).resolve().parents[2]/'scripts/host/cycles_captured_verifier_test.py')]))
 commands.append(('cycles-live-burst-classifier-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_live_burst_classifier_test.py')]))
 commands.append(('cycles-live-burst-snapshot-admission-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_burst_snapshot_test.py')]))
 commands.append(('cycles-displacement-profile-admission-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_displacement_profile_test.py')]))
 commands.append(('cycles-displacement-candidate-admission-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/cycles_displacement_candidate_test.py')]))
 commands.append(('native-height-verifier-admission-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/native_height_verifier_test.py')]))
 commands.append(('native-material-projection-admission-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/native_material_projection_admission_test.py')]))
 commands.append(('interactive-brush-gpu-transport-source-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/bm_gpu_safety_source_test.py')]))
 commands.append(('material-layer-parameter-packet-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/material_layer_parameter_verifier_test.py')]))
 commands.append(('material-layer-surface-causal-cpu',[sys.executable,str(Path(__file__).resolve().parents[2]/'native/tests/material_layer_surface_verifier_test.py')]))
 commands.append(('eevee-completion-observation-values-cpu',[sys.executable,str(Path(__file__).with_name('verify_eevee_completion_test.py'))]))
commands.append(('bts-requested-features-native',[str(a.build_dir/'bin/mpainter'),'--cpu-export-test',str(a.output/'requested-features')]))
commands.append(('bts-export-files',[str(a.build_dir/'bin/mpainter'),'--cpu-export-test',str(a.output/'export')]))
if not a.export_only:commands.append(('bts-asset-dragdrop-workspace',[str(a.build_dir/'bin/mpainter'),'--cpu-asset-dragdrop-test',str(a.output/'asset-dragdrop-workspace')]))
if not a.export_only:commands.append(('bts-workspace-shutdown-ui',[str(a.build_dir/'bin/mpainter'),'--cpu-workspace-shutdown-test']))
if not a.export_only:commands.append(('bts-native-material-ui',[str(a.build_dir/'bin/mpainter'),'--cpu-native-material-test',str(a.output/'native-material-ui')]))
if not a.export_only:commands.append(('bts-texture-import-files',[str(a.build_dir/'bin/mpainter'),'--cpu-texture-import-test',str(a.output/'texture-import')]))
if not a.export_only:commands.append(('bts-skew-rays',[str(a.build_dir/'bin/mpainter'),'--cpu-skew-test',str(a.output/'skew')]))
if a.plan_only:
 (a.output/'plan.json').write_text(json.dumps({'cpu_only':True,'BTS_NO_BACKGROUND_GPU':env['BTS_NO_BACKGROUND_GPU'],'QT_QPA_OFFSCREEN_NO_GLX':env['QT_QPA_OFFSCREEN_NO_GLX'],'commands':commands},indent=2)+'\n')
 print(f'{len(commands)} explicitly planned CPU checks; no test process started.')
 raise SystemExit(0)
for name,command in commands:
 with (a.output/(name+'.log')).open('w') as log:
  test_env=dict(env,LC_ALL='sv_SE.UTF-8',LANG='sv_SE.UTF-8') if name=='bts-numeric-locale-tests' else env
  if name=='bts-requested-features-native':test_env=dict(test_env,BTS_REQUESTED_FEATURES_PROOF='1')
  try:
   code=subprocess.run(command,cwd=a.output,env=test_env,stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
  except subprocess.TimeoutExpired:
   code=124
   log.write('\nFAILED: check exceeded the 120-second timeout.\n')
  except OSError as exc:
   code=127
   log.write('\nFAILED: could not start check: '+str(exc)+'\n')
 results.append(dict(test=name,returncode=code))
 # Preserve every finished result even if a later check or the runner fails.
 result_path=a.output/'results.json';temporary=result_path.with_suffix('.json.tmp')
 temporary.write_text(json.dumps(results,indent=2)+'\n');temporary.replace(result_path)
 if code:print(name,'FAILED:',(a.output/(name+'.log')).read_text()[-3000:],flush=True)
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
failed=sum(bool(r['returncode']) for r in results);print(f'{len(results)} CPU checks, {failed} failed; native PNG export included.',flush=True)
raise SystemExit(bool(failed))
