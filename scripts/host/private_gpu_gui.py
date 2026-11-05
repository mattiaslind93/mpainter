"""Owned Xvfb + NVIDIA EGL presentation; never connects windows to the desktop."""
from contextlib import contextmanager
from pathlib import Path
import json,os,subprocess,time

@contextmanager
def private_gpu_gui(output,environment,root):
    from session_guard import require_gpu_idle, require_gui_allowed
    require_gpu_idle(environment=environment)
    output=Path(output).resolve();root=Path(root).resolve()
    bts_root=Path(environment.get('BTS_ROOT',root.parent)).resolve()
    build=bts_root/'build'
    if not any(parent.exists()and os.path.samefile(parent,build)for parent in (output,*output.parents)):
        raise ValueError('Private GUI output must be inside BTS build')
    output.mkdir(parents=True,exist_ok=True)
    env=dict(environment)
    for key in ('WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD'):
        env.pop(key,None)
    runtime=output/'runtime';runtime.mkdir(exist_ok=True,mode=0o700);runtime.chmod(0o700)
    env.update(QT_QPA_PLATFORM='xcb',BTS_HEADLESS_WORKSPACE='0',QT_XCB_GL_INTEGRATION='xcb_glx',XDG_RUNTIME_DIR=str(runtime),VGL_EGLLIB='/usr/lib64/libEGL.so.1')
    for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources')]:
        (output/folder).mkdir(exist_ok=True);env[key]=str(output/folder)
    qt_root=Path(env.get('QT_ROOT_DIR',bts_root/'toolchains/qt'))
    if not qt_root.is_dir():qt_root=bts_root/'qt/6.8.3'
    env['BTS_ROOT']=str(bts_root);env['QT_PLUGIN_PATH']=str(qt_root/'plugins')
    env['LD_LIBRARY_PATH']=str(qt_root/'lib')+':'+env.get('LD_LIBRARY_PATH','')
    if not env.get('BTS_ENVIRONMENT'):
        config=Path(env.get('BTS_USER_CONFIG_DIR',root.parent.parent/'.config'))
        for version in ('5.2','5.1','5.0'):
            candidate=config/f'blender/{version}/datafiles/studiolights/world/TT_environmentLights_v001.hdr'
            if candidate.is_file():env['BTS_ENVIRONMENT']=str(candidate);break
    number=next((str(n)for n in range(90,200)if not Path(f'/tmp/.X{n}-lock').exists()and not Path(f'/tmp/.X11-unix/X{n}').exists()),None)
    if number is None:raise RuntimeError('No free private Xvfb display')
    with (output/'xvfb.log').open('w')as log:
        server=subprocess.Popen(['Xvfb',':'+number,'-screen','0','1600x1000x24','-nolisten','tcp','-ac'],env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+10;lock=Path(f'/tmp/.X{number}-lock')
            while time.monotonic()<deadline:
                if server.poll()is not None:raise RuntimeError('Owned Xvfb exited during startup')
                if lock.exists()and lock.read_text().strip()==str(server.pid)and Path(f'/tmp/.X11-unix/X{number}').exists():break
                time.sleep(.05)
            else:raise RuntimeError('Owned Xvfb readiness timed out')
            env['BTS_PRIVATE_XVFB_PID']=str(server.pid)
            env['DISPLAY']=':'+number
            require_gui_allowed(allow_private_xvfb=True,environment=env)
            # The 3D backend is an EGL device, never the default :0 X display.
            prefix=['bash','-lc','source "$1/scripts/host/gpu-env.sh"; unset WAYLAND_DISPLAY XAUTHORITY; shift; exec vglrun -nodl -d egl -c proxy "$@"','bts-private',str(root)]
            with (output/'private-renderer.log').open('w')as renderer:
                probe=subprocess.run(prefix+['glxinfo','-B'],env=env,stdout=renderer,stderr=subprocess.STDOUT,timeout=30)
            text=(output/'private-renderer.log').read_text()
            if probe.returncode or 'NVIDIA GeForce RTX 3090'not in text:raise RuntimeError('Private display did not verify RTX rendering: '+text[-1500:])
            (output/'private-display.json').write_text(json.dumps({'display':env['DISPLAY'],'owned_server_pid':server.pid,'renderer':'NVIDIA GeForce RTX 3090','backend':'VirtualGL EGL device; xcb window on owned Xvfb','desktop_input':False},indent=2)+'\n')
            yield prefix,env
        finally:
            if server.poll()is None:server.terminate()
            server.wait(timeout=10)
