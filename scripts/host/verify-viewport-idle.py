"""Full workspace idle profile on a PRIVATE Xvfb display; never artist input.

Software rendering measures Qt idle scheduling, not RTX viewport throughput.
Use --headless-viewport-test separately for the NVIDIA GPU path.
"""
import argparse,json,os,signal,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--project',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];bts=root.parent
xvfb=subprocess.Popen(['distrobox','enter','bts-build','--','bash','-lc','printf "BTS_XVFB_PID=%s\\n" "$$"; exec nice -n 19 Xvfb -displayfd 1 -screen 0 1440x900x24 -nolisten tcp -noreset'],stdout=subprocess.PIPE,stderr=(a.output/'xvfb.log').open('w'),text=True)
try:
 pid_line=xvfb.stdout.readline().strip();assert pid_line.startswith('BTS_XVFB_PID='),pid_line;owned_xvfb_pid=int(pid_line.split('=',1)[1]);display=xvfb.stdout.readline().strip();assert display.isdigit(),display
 env=dict(os.environ,DISPLAY=':'+display,QT_QPA_PLATFORM='xcb',QT_XCB_GL_INTEGRATION='xcb_glx',LIBGL_ALWAYS_SOFTWARE='1',__GLX_VENDOR_LIBRARY_NAME='mesa',LP_NUM_THREADS='2',OMP_NUM_THREADS='2',BTS_ROOT=str(bts),XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_IDLE_DEMO_DIR=str(a.output),BTS_PERFORMANCE_PROFILE='1',QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib'))
 for name in ('BTS_SESSION_PROFILE','BTS_TEST_CONTINUOUS_FRAMES','__EGL_VENDOR_LIBRARY_FILENAMES'):env.pop(name,None)
 command=['distrobox','enter','bts-build','--','env','-u','BTS_SESSION_PROFILE','-u','BTS_TEST_CONTINUOUS_FRAMES','-u','__EGL_VENDOR_LIBRARY_FILENAMES']+[key+'='+value for key,value in env.items()if os.environ.get(key)!=value]+['nice','-n','19',str(bts/'build/host/bin/bts-host'),'--workspace','--open-document',str(a.project.resolve())]
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=180)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
 result=json.loads((a.output/'idle.json').read_text());assert result['idle_target_pass'],result
 phases={}
 for row in result['performance']:phases.setdefault(row['phase'],[]).append(row['wall_ms'])
 import statistics
 result['phase_median_ms']={key:statistics.median(values)for key,values in phases.items()if key in ('draw_symmetry','draw','gpu_render')}
 (a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({key:value for key,value in result.items()if key!='performance'},indent=2))
finally:
 if 'owned_xvfb_pid' in locals():
  try:os.kill(owned_xvfb_pid,signal.SIGTERM)
  except ProcessLookupError:pass
 xvfb.terminate();xvfb.wait(timeout=15)
