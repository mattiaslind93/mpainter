#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only an offscreen editor, with all writable state inside the workspace."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--timeout',type=int,default=600)
p.add_argument('--executable',type=Path)
p.add_argument('--bundled',action='store_true',help='Use only resources deployed alongside --executable')
p.add_argument('--trace-libraries',action='store_true')
p.add_argument('--env',action='append',default=[])
p.add_argument('args',nargs=argparse.REMAINDER)
a=p.parse_args()
work=Path(__file__).resolve().parents[3]
out=a.output.resolve()
if not out.is_relative_to(work):p.error('Output must be inside mpainter-build')
out.mkdir(parents=True,exist_ok=False)
priority=os.getpriority(os.PRIO_PROCESS,0)
if priority<15:os.nice(15-priority)
env=os.environ.copy()
for name,leaf in {
 'XDG_CONFIG_HOME':'mpainter-test-sessions/config','XDG_DATA_HOME':'data',
 'XDG_CACHE_HOME':'cache','CFFIXED_USER_HOME':'foundation-home',
 'BLENDER_USER_RESOURCES':'blender-user','BLENDER_USER_CONFIG':'blender-config',
 'BLENDER_USER_DATAFILES':'blender-data','BLENDER_USER_SCRIPTS':'blender-scripts',
 'BLENDER_USER_EXTENSIONS':'blender-extensions','MPAINTER_CACHE_DIR':'blender-cache',
 'BTS_RESOURCE_CACHE':'resource-cache','BTS_RECOVERY_DIR':'mpainter-test-sessions/recovery','TMPDIR':'tmp',
}.items():
 path=out/leaf;path.mkdir(parents=True);env[name]=str(path)
exe=(a.executable or work/'build-metal/bin/mPainter.app/Contents/MacOS/mPainter').resolve()
if not exe.is_relative_to(work):p.error('Executable must be inside mpainter-build')
runtime=exe.parent.parent/'Resources/5.2' if a.bundled else work/'build-metal/runtime/Blender.app/Contents/Resources/5.2'
if a.bundled:
 for key in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','DYLD_LIBRARY_PATH','DYLD_FRAMEWORK_PATH']:
  env.pop(key,None)
env.update(GIT_CONFIG_GLOBAL=str(work/'toolchains/gitconfig'),GIT_CONFIG_NOSYSTEM='1',
 QT_QPA_PLATFORM='offscreen',QT_QPA_OFFSCREEN_NO_GLX='1',BTS_HEADLESS_WORKSPACE='1',
 BTS_HOST_THREADS='6',OMP_NUM_THREADS='6',OPENBLAS_NUM_THREADS='6',PYTHONDONTWRITEBYTECODE='1',
 BLENDER_SYSTEM_RESOURCES=str(runtime),BLENDER_SYSTEM_SCRIPTS=str(runtime/'scripts'),
 BLENDER_SYSTEM_DATAFILES=str(runtime/'datafiles'),BLENDER_SYSTEM_PYTHON=str(runtime/'python'),
 BTS_ENVIRONMENT=str(runtime/'datafiles/studiolights/world/studio.exr'))
if a.bundled:
 # Prove the app discovers its resources exactly as a normal launch would.
 for key in ['BLENDER_SYSTEM_RESOURCES','BLENDER_SYSTEM_SCRIPTS','BLENDER_SYSTEM_DATAFILES','BLENDER_SYSTEM_PYTHON','BTS_ENVIRONMENT']:
  env.pop(key,None)
if a.trace_libraries:env['DYLD_PRINT_LIBRARIES']='1'
for item in a.env:
 key,value=item.split('=',1)
 if not key.startswith(('BTS_','MPAINTER_')):p.error('Only test variables can be overridden')
 env[key]=value.replace("{output}",str(out))
args=a.args[1:] if a.args[:1]==['--'] else a.args
args=[s.replace('{output}',str(out)) for s in args]
command=[str(exe),'--no-splash',*args]
binary_sha256=hashlib.file_digest(exe.open('rb'),'sha256').hexdigest()
start=time.monotonic()
with (out/'editor.log').open('w') as log:
 try:
  result=subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout)
  data={'exit_code':result.returncode}
 except subprocess.TimeoutExpired:data={'timeout':a.timeout}
errors=[line for line in (out/'editor.log').read_text(errors='replace').splitlines()
        if re.search(r'gpu\.shader.*ERROR|BTS_GPU_STACK_ERROR|BTS_GPU_STACK_PROGRAM_FAILED|BTS_DRAW_FAILED|BTS_METAL_INIT_FAILED|Generated dependency not found',line)]
data['gpu_errors']=errors[:30]
data['passed']=data.get('exit_code')==0 and not errors
if a.trace_libraries:
 libraries=sorted(set(re.findall(r'^dyld\[\d+\]: <[^>]+> (/.+)$',(out/'editor.log').read_text(errors='replace'),re.M)))
 app=exe.parent.parent.parent
 external=[path for path in libraries if not path.startswith(('/System/','/usr/lib/','/Library/Apple/System/Library/')) and not Path(path).resolve().is_relative_to(app)]
 data['loaded_libraries']=libraries;data['external_non_system_libraries']=external
 data['passed']=data['passed'] and len(libraries)>10 and not external
data.update(command=command,seconds=time.monotonic()-start,platform='offscreen',binary_sha256=binary_sha256,bundled_resources=a.bundled,runtime=str(runtime))
(out/'result.json').write_text(json.dumps(data,indent=2)+'\n')
print(json.dumps(data),flush=True)
raise SystemExit(0 if data['passed'] else 1)
