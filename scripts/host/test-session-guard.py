"""Headless /proc fixtures: protect real sessions, permit isolated test processes."""
import tempfile
from pathlib import Path
from session_guard import user_sessions, graphics_sessions, cpu_test_arguments, owned_private_xvfb, require_gui_allowed
from unittest.mock import patch
with tempfile.TemporaryDirectory(prefix='session-guard-') as d:
 root=Path(d)
 def process(pid,args,config='',platform='xcb'):
  p=root/str(pid);p.mkdir(exist_ok=True);(p/'comm').write_text('bts-host\n');(p/'cmdline').write_bytes(('\0'.join(['bts-host']+args)+'\0').encode());(p/'environ').write_bytes(f'XDG_CONFIG_HOME={config}\0QT_QPA_PLATFORM={platform}\0'.encode())
 process(1,['--workspace'],'/home/mlind/bts/home/.config')
 process(2,['--workspace','--gui-fix-demo','/test'],'/home/mlind/bts/build/v-native/config')
 process(3,['--workspace','--gui-fix-demo','/test'],'/home/mlind/.config')
 process(4,['--workspace','--gui-fix-demo','/test'],'/home/mlind/bts/build/test/config','offscreen')
 process(5,['--cpu-export-test','/test'])
 process(6,['--workspace','--open-document','/artist/demo.blend'],'/home/mlind/.config')
 assert [x['pid'] for x in user_sessions(root,exclude=-1)]==[1,3,6]
 assert [x['pid'] for x in user_sessions(root,exclude=1)]==[3,6]
 assert [x['pid'] for x in graphics_sessions(root,exclude=-1)]==[1,2,3,4,6]
 process(7,[]);(root/'7/comm').write_text('blender\n')
 assert graphics_sessions(root,exclude=-1)[-1]['pid']==7
 (root/'7/status').write_text('State:\tZ (zombie)\n')
 assert all(x['pid']!=7 for x in graphics_sessions(root,exclude=-1))
 assert cpu_test_arguments(['app','--cpu-native-material-test','/tmp/proof'])
 assert cpu_test_arguments(['app','--cpu-asset-dragdrop-test','/tmp/proof'])
 assert not cpu_test_arguments(['app','--cpu-asset-dragdrop-test','/tmp/proof','--workspace'])
 assert cpu_test_arguments(['app','--prepare-native-craftypack','/tmp/assets','--craftypack-source','/tmp/source'])
 assert cpu_test_arguments(['app','--prepare-native-craftypack=/tmp/assets','--craftypack-source=/tmp/source'])
 for suffix in (['--workspace'],['--headless-viewport-test','/tmp/proof'],['--gui-fix-demo','/tmp/proof']):
  assert not cpu_test_arguments(['app','--prepare-native-craftypack','/tmp/assets',*suffix])
 process(8,['--prepare-native-craftypack','/tmp/assets','--craftypack-source','/tmp/source'])
 assert all(x['pid']!=8 for x in graphics_sessions(root,exclude=-1))
 process(9,[],'/artist/config');(root/'9/comm').write_text('mpainter\n')
 assert any(x['pid']==9 for x in graphics_sessions(root,exclude=-1))
 assert any(x['pid']==9 for x in user_sessions(root,exclude=-1))
 process(10,['--cpu-export-test','/tmp/proof']);(root/'10/comm').write_text('mpainter\n')
 assert all(x['pid']!=10 for x in graphics_sessions(root,exclude=-1))
 assert all(x['pid']!=10 for x in user_sessions(root,exclude=-1))
 for args in (['app','--cpu-unknown'],['app','--headless-viewport-test','/tmp/proof'],['app','--cpu-export-test','/tmp/proof','--gui-fix-demo','/tmp/proof'],['app','--cpu-export-test','/tmp/proof','--workspace']):assert not cpu_test_arguments(args)
 import subprocess,sys,ctypes,time,os
 fake=subprocess.Popen([sys.executable,'-c',"import ctypes,time;ctypes.CDLL(None).prctl(15,b'bts-host',0,0,0);print('ready',flush=True);time.sleep(20)",'--workspace'],stdout=subprocess.PIPE,text=True)
 try:
  assert fake.stdout.readline().strip()=='ready'
  from parity_test_environment import private_display
  blocked_env=dict(os.environ,QT_QPA_PLATFORM='xcb',DISPLAY=private_display())
  result=subprocess.run(['bash',str(Path(__file__).parent/'run-host.sh'),'--workspace','--gui-fix-demo',str(root/'blocked')],env=blocked_env,capture_output=True,text=True)
  assert result.returncode==77 and 'BTS_GUI_SKIPPED' in result.stderr,(result.returncode,result.stderr)
 finally:fake.terminate();fake.wait(timeout=5)
 server=root/'700';server.mkdir();(server/'comm').write_text('Xvfb\n');(server/'cmdline').write_bytes(b'/usr/bin/Xvfb\0:99\0-screen\00\0');(server/'status').write_text('PPid:\t400\n')
 caller=root/'500';caller.mkdir();(caller/'status').write_text('PPid:\t400\n')
 locks=root/'locks';locks.mkdir();lock=locks/'.X99-lock';lock.write_text('700\n')
 env={'BTS_ROOT':'/home/mlind/bts','XDG_CONFIG_HOME':'/home/mlind/bts/build/owned/config','QT_QPA_PLATFORM':'xcb','DISPLAY':':99','BTS_PRIVATE_XVFB_PID':'700'}
 def owned(changes=None):return owned_private_xvfb(root,locks,environment=env|dict(changes or {}),caller_pid=500)
 assert owned() and owned({'DISPLAY':':99.0'}) and owned({'QT_QPA_PLATFORM':'offscreen'})
 # Verifiers and cleanup wrappers may sit between xvfb-run and run-host.
 wrapper=root/'450';wrapper.mkdir();(wrapper/'status').write_text('PPid:\t400\n')
 (caller/'status').write_text('PPid:\t450\n');assert owned()
 (wrapper/'status').write_text('PPid:\t450\n');assert not owned()
 (caller/'status').write_text('PPid:\t400\n')
 for change in ({'DISPLAY':':0'},{'DISPLAY':':98'},{'DISPLAY':':65536'},{'BTS_PRIVATE_XVFB_PID':'0'},{'BTS_PRIVATE_XVFB_PID':'701'},{'WAYLAND_DISPLAY':'wayland-0'},{'QT_QPA_PLATFORM':'wayland'},{'XDG_CONFIG_HOME':'/home/mlind/.config'},{'XDG_CONFIG_HOME':'/home/mlind/bts/build/owned/../../outside/config'}):assert not owned(change),change
 (server/'comm').write_text('ordinary\n');assert not owned();(server/'comm').write_text('Xvfb\n')
 (server/'cmdline').write_bytes(b'/usr/bin/Xvfb\0:98\0');assert not owned();(server/'cmdline').write_bytes(b'/usr/bin/Xvfb\0:99\0')
 (caller/'status').write_text('PPid:\t401\n');assert not owned();(caller/'status').write_text('PPid:\t400\n')
 lock.write_text('701\n');assert not owned();lock.write_text('700\n')
 with patch('session_guard.os.getuid',return_value=os.getuid()+1):assert not owned()
 with patch.dict(os.environ,{'BTS_NO_BACKGROUND_GPU':'0'}),patch('session_guard.graphics_sessions',return_value=[]),patch('session_guard.user_sessions',return_value=[]),patch('session_guard.display_dpms_off',return_value=True),patch('session_guard.owned_private_xvfb',return_value=True):
  require_gui_allowed(allow_private_xvfb=True,caller_pid=500)
  try:require_gui_allowed(caller_pid=500)
  except SystemExit as blocked:assert blocked.code==77
  else:raise AssertionError('Unverified display bypassed sleeping monitor guard')
  with patch('session_guard.owned_private_xvfb',return_value=False):
   try:require_gui_allowed(allow_private_xvfb=True,caller_pid=500)
   except SystemExit as blocked:assert blocked.code==77
   else:raise AssertionError('Private flag without ownership bypassed sleeping monitor guard')
  with patch.dict(os.environ,{'BTS_NO_BACKGROUND_GPU':'1'}):
   try:require_gui_allowed(allow_private_xvfb=True,caller_pid=500)
   except SystemExit as blocked:assert blocked.code==77
   else:raise AssertionError('Private display bypassed explicit GPU ban')
  with patch('session_guard.graphics_sessions',return_value=[{'pid':42}]):
   try:require_gui_allowed(allow_private_xvfb=True,caller_pid=500)
   except SystemExit as blocked:assert blocked.code==77
   else:raise AssertionError('Private display bypassed active graphics session')
 with patch('session_guard.user_sessions',return_value=[{'pid':42}]),patch('session_guard.owned_private_xvfb',return_value=True):
  try:require_gui_allowed()
  except SystemExit as blocked:assert blocked.code==77
  else:raise AssertionError('Default input-injection guard allowed a private display')
  try:require_gui_allowed(allow_private_xvfb=True,caller_pid=500)
  except SystemExit as blocked:assert blocked.code==77
  else:raise AssertionError('Private Xvfb bypassed an active artist session')
 from private_gpu_gui import private_gpu_gui
 with patch.dict(os.environ,{'BTS_NO_BACKGROUND_GPU':'1'}),patch('private_gpu_gui.subprocess.Popen',side_effect=AssertionError('GPU process spawned')):
  try:
   with private_gpu_gui(root/'must-not-exist',{},Path(__file__).resolve().parents[2]):pass
  except SystemExit as blocked:assert blocked.code==77
  else:raise AssertionError('Private helper bypassed GPU ban')
 assert not (root/'must-not-exist').exists()
 with patch.dict(os.environ,{'BTS_NO_BACKGROUND_GPU':'0'}),patch('private_gpu_gui.subprocess.Popen',side_effect=AssertionError('GPU process spawned')):
  try:
   with private_gpu_gui(root/'must-not-exist',{'BTS_NO_BACKGROUND_GPU':'1'},Path(__file__).resolve().parents[2]):pass
  except SystemExit as blocked:assert blocked.code==77
  else:raise AssertionError('Private helper ignored child GPU ban')
 fake_host=root/'cpu-sentinel';fake_host.write_text('#!/usr/bin/env python3\nimport json,os\nfrom pathlib import Path\nPath(os.environ["BTS_CPU_SENTINEL"]).write_text(json.dumps({k:os.environ.get(k) for k in ["QT_QPA_PLATFORM","QT_QPA_OFFSCREEN_NO_GLX","BTS_NO_BACKGROUND_GPU"]}))\n');fake_host.chmod(0o700)
 sentinel=root/'cpu-sentinel.json';launcher=Path(__file__).parent/'run-host.sh'
 cpu_env=dict(os.environ,BTS_NO_BACKGROUND_GPU='1',BTS_HOST_EXECUTABLE=str(fake_host),BTS_CPU_SENTINEL=str(sentinel),BTS_SESSION_LOG_DIR=str(root/'logs'),XDG_CONFIG_HOME=str(root/'config'),BTS_RECOVERY_DIR=str(root/'recovery'))
 result=subprocess.run(['bash',str(launcher),'--cpu-native-material-test',str(root/'cpu')],env=cpu_env,capture_output=True,text=True)
 assert result.returncode==0 and sentinel.exists(),(result.returncode,result.stderr)
 import json
 assert json.loads(sentinel.read_text())=={'QT_QPA_PLATFORM':'offscreen','QT_QPA_OFFSCREEN_NO_GLX':'1','BTS_NO_BACKGROUND_GPU':'1'}
 sentinel.unlink()
 result=subprocess.run(['bash',str(launcher),'--prepare-native-craftypack',str(root/'assets'),'--craftypack-source',str(root/'source')],env=cpu_env,capture_output=True,text=True)
 assert result.returncode==0 and sentinel.exists(),(result.returncode,result.stderr)
 assert json.loads(sentinel.read_text())=={'QT_QPA_PLATFORM':'offscreen','QT_QPA_OFFSCREEN_NO_GLX':'1','BTS_NO_BACKGROUND_GPU':'1'}
 sentinel.unlink()
 result=subprocess.run(['bash',str(launcher),'--prepare-native-craftypack',str(root/'assets'),'--headless-viewport-test',str(root/'gpu')],env=cpu_env,capture_output=True,text=True)
 assert result.returncode==77 and not sentinel.exists(),(result.returncode,result.stderr)
 for arguments in (['--workspace'],['--headless-viewport-test',str(root/'gpu')],['--cpu-unknown'],['--cpu-export-test',str(root/'cpu'),'--gui-fix-demo',str(root/'gpu')]):
  result=subprocess.run(['bash',str(launcher),*arguments],env=cpu_env,capture_output=True,text=True)
  assert result.returncode==77 and not sentinel.exists(),(arguments,result.returncode,result.stderr)
 print('session guard: CPU whitelist, active Blender/private hosts, explicit ban and no-GLX launcher PASS')
 print('private Xvfb ownership checks PASS; ownership never bypasses active-session/GPU ban')
