# SPDX-License-Identifier: GPL-3.0-or-later
"""Protect an interactive user's BTS session from GUI demos and input injection."""
import os
from pathlib import Path
import re
import sys

CPU_TEST_OPTIONS = frozenset((
    '--cpu-asset-dragdrop-test', '--cpu-workspace-shutdown-test', '--cpu-native-material-test',
    '--prepare-native-craftypack',
    '--cpu-bake-source-test', '--cpu-skew-test', '--cpu-texture-import-test',
    '--cpu-export-test',
))

def cpu_test_arguments(arguments):
    options = {arg.split('=', 1)[0] for arg in arguments if arg.startswith('--')}
    return bool(options & CPU_TEST_OPTIONS) and not any(
        option not in CPU_TEST_OPTIONS and
        ('demo' in option or option.startswith(('--cpu-', '--headless-')) or
         option in ('--workspace', '--screenshot', '--state-json', '--quit-after', '--exit-after'))
        for option in options)

def graphics_sessions(proc=Path('/proc'), exclude=None):
    """An isolated display still shares the NVIDIA device and BAR mappings."""
    found=[]
    for entry in proc.iterdir():
        if not entry.name.isdigit() or int(entry.name)==(os.getpid() if exclude is None else exclude):
            continue
        try:
            name=(entry/'comm').read_text().strip()
            if name not in ('bts-host', 'mpainter', 'mPainter', 'blender'):continue
            status=entry/'status'
            if status.exists() and any(line.startswith('State:') and line.split()[1]=='Z' for line in status.read_text().splitlines()):continue
            args=(entry/'cmdline').read_bytes().decode(errors='replace').rstrip('\0').split('\0')
            if name!='blender' and cpu_test_arguments(args):continue
            found.append({'pid':int(entry.name),'name':name})
        except (OSError,ValueError,ProcessLookupError):continue
    return sorted(found,key=lambda item:item['pid'])

def is_test_command(arguments):
    return any((value.startswith('--') and 'demo' in value) or value.startswith(('--cpu-', '--headless-')) or value.split('=', 1)[0] in ('--prepare-native-craftypack', '--screenshot', '--state-json', '--quit-after', '--exit-after') for value in arguments[1:])

def user_sessions(proc=Path('/proc'), exclude=None):
    found=[]
    for entry in proc.iterdir():
        if not entry.name.isdigit() or int(entry.name)==(os.getpid() if exclude is None else exclude):
            continue
        try:
            name=(entry/'comm').read_text().strip()
            if name not in ('bts-host','mpainter','mPainter'):continue
            args=(entry/'cmdline').read_bytes().decode(errors='replace').rstrip('\0').split('\0')
            if name=='bts-host' and '--workspace' not in args:continue
            if cpu_test_arguments(args):continue
            env=dict(item.split('=',1) for item in (entry/'environ').read_bytes().decode(errors='replace').split('\0') if '=' in item)
            platform=env.get('QT_QPA_PLATFORM','')
            config=env.get('XDG_CONFIG_HOME','')
            # A GUI demo is a test only when it also has isolated preferences.
            isolated=('/bts/build/' in config or '/mpainter-test-sessions/' in config) and config.endswith(('/config','/.config'))
            if not is_test_command(args) or not isolated:
                found.append({'pid':int(entry.name),'config':config})
        except (OSError,ValueError,ProcessLookupError):continue
    return sorted(found,key=lambda item:item['pid'])

def owned_private_xvfb(proc=Path('/proc'), locks=Path('/tmp'), *, environment=None, caller_pid=None):
    """Match the native guard: an isolated test may use its launcher's Xvfb.

    This does not authorize input injection, which remains globally disruptive.
    run-host calls from a child Python guard, so it supplies the launch shell PID.
    """
    env=os.environ if environment is None else environment
    if env.get('QT_QPA_PLATFORM') not in ('xcb','offscreen') or env.get('WAYLAND_DISPLAY'):return False
    try:
        config=Path(env.get('XDG_CONFIG_HOME',''))
        build=(Path(env.get('BTS_ROOT',str(Path.home()/'bts')))/'build').resolve()
        if not config.is_absolute() or config.name not in ('config','.config') or not config.resolve().is_relative_to(build):return False
        display=re.fullmatch(r':([1-9][0-9]*)(?:\.0)?',env.get('DISPLAY',''))
        if not display or int(display[1])>65535:return False
        display=':'+display[1]
        pid=int(env.get('BTS_PRIVATE_XVFB_PID','0'))
        if pid<=1:return False
        server=proc/str(pid)
        caller=proc/str(os.getpid() if caller_pid is None else caller_pid)
        if server.stat().st_uid!=os.getuid() or caller.stat().st_uid!=os.getuid():return False
        args=(server/'cmdline').read_bytes().decode().rstrip('\0').split('\0')
        if not args or Path(args[0]).name!='Xvfb' or display not in args or (server/'comm').read_text().strip()!='Xvfb':return False
        def parent(path):
            return next(int(line.split(':',1)[1]) for line in (path/'status').read_text().splitlines() if line.startswith('PPid:'))
        if int((locks/('.X'+display[1:]+'-lock')).read_text().strip())!=pid:return False
        owner=parent(server)
        ancestor=int(caller.name)
        for _ in range(32):
            if ancestor<=1:return False
            if ancestor==owner:return True
            next_parent=parent(proc/str(ancestor))
            if next_parent==ancestor:return False
            ancestor=next_parent
        return False
    except (OSError,ValueError,StopIteration,UnicodeError):return False

def display_dpms_off(drm=Path('/sys/class/drm')):
    try:
        if not drm.is_dir():return False
        connected=0;active=0
        for entry in drm.iterdir():
            status=entry/'status'
            if not status.is_file() or status.read_text().strip()!='connected':continue
            connected+=1
            dpms=(entry/'dpms').read_text().strip() if (entry/'dpms').is_file() else 'On'
            enabled=(entry/'enabled').read_text().strip() if (entry/'enabled').is_file() else 'enabled'
            if dpms=='On' and enabled!='disabled':active+=1
        return connected>0 and active==0
    except OSError:return False

def require_gpu_idle(*, environment=None):
    """Check shared-device exclusions before even creating a private display."""
    banned=os.environ.get('BTS_NO_BACKGROUND_GPU')=='1' or (environment or {}).get('BTS_NO_BACKGROUND_GPU')=='1'
    sessions=graphics_sessions() if not banned else []
    if banned or sessions or user_sessions():
        reason='BTS_NO_BACKGROUND_GPU=1' if banned else 'graphics_session_active pid='+','.join(str(s['pid']) for s in sessions)
        print('BTS_GUI_SKIPPED '+reason+'; no GPU/GUI/OpenGL test started',file=sys.stderr,flush=True)
        raise SystemExit(77)

def require_gui_allowed(*, allow_private_xvfb=False, caller_pid=None, environment=None):
    # Private displays still share GPU resources; never bypass session exclusions.
    require_gpu_idle(environment=environment)
    private=allow_private_xvfb and owned_private_xvfb(environment=environment,caller_pid=caller_pid)
    if not private and display_dpms_off():
        reason='display_dpms_off'
        print('BTS_GUI_SKIPPED '+reason+'; no GPU/GUI/OpenGL test started',file=sys.stderr,flush=True)
        raise SystemExit(77)

if __name__=='__main__':
    sessions=user_sessions()
    if sessions:
        print('BTS_USER_SESSION_ACTIVE pid='+','.join(str(s['pid']) for s in sessions),flush=True)
    raise SystemExit(0 if sessions else 1)
