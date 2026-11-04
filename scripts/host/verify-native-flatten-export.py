"""Build an isolated native host overlay; verify GPU Flatten then standard Export.

Run with nice -n19 inside the build container. --compile-only / --run-only lets
GPU correctness contexts be scheduled separately. Input host build must be stable.
"""
import argparse,hashlib,json,os,shlex,shutil,subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--freeze-inputs',action='store_true',help='Reflink all mutable host .o/.a link inputs before compiling');group=p.add_mutually_exclusive_group();group.add_argument('--compile-only',action='store_true');group.add_argument('--run-only',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];build=a.build_dir.resolve();out=a.output.resolve();assert out.is_relative_to(root.parent/'build');out.mkdir(parents=True,exist_ok=True)
if not a.run_only:
 commands_file=out/'frozen-commands.json';inputs_file=out/'frozen-link-inputs.json'
 commands=json.loads(commands_file.read_text())if commands_file.exists()else subprocess.check_output([shutil.which('ninja')or'ninja-build','-t','commands','bts-host'],cwd=build,text=True).splitlines();frozen=json.loads(inputs_file.read_text())if inputs_file.exists()else{}
 if a.freeze_inputs and not frozen:
  snapshot=out/'link-inputs';snapshot.mkdir(exist_ok=True)
  for token in shlex.split(commands[-1]):
   if not token.endswith(('.o','.a')):continue
   path=(build/token).resolve()
   if not path.is_relative_to(build):continue
   target=snapshot/(hashlib.sha256(token.encode()).hexdigest()[:16]+'-'+path.name);subprocess.run(['cp','--reflink=auto','-p',str(path),str(target)],check=True);frozen[token]=str(target)
  commands_file.write_text(json.dumps(commands));inputs_file.write_text(json.dumps(frozen))
 objects={}
 def compile(name):
  c=shlex.split(next(v for v in commands if ' -c 'in v and v.endswith('/'+name)));old=c[c.index('-o')+1];obj=str(out/(name+'.o'));c[c.index('-o')+1]=obj;c[c.index('-c')+1]=str(root/'native/host'/name)
  for flag,value in [('-MF',obj+'.d'),('-MT',obj)]:
   if flag in c:c[c.index(flag)+1]=value
  c.insert(2 if Path(c[0]).name=='ccache'else 1,'-I'+str(root/'native/host'));c=['-O1'if v in ['-O2','-O3']else v for v in c];print('Compile',name,flush=True);subprocess.run(c,cwd=build,check=True);return old,obj
 with ThreadPoolExecutor(max_workers=2)as pool:objects=dict(pool.map(compile,['NativeMaterialHostQueueDemo.cc','TextureExport.cc']))
 link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link
 if '&&'in link:link=link[:link.index('&&')]
 link=[objects.get(v,frozen.get(v,v))for v in link];link=['-Wl,--dependency-file='+str(out/'link.d')if v.startswith('-Wl,--dependency-file=')else v for v in link];link[link.index('-o')+1]=str(out/'bts-host');subprocess.run(link,cwd=build,check=True)
 for name in ['lib','5.2']:
  if not(out/name).exists():subprocess.run(['cp','-a','--reflink=auto',str(build/'bin'/name),str(out/name)],check=True)
if not a.compile_only:
 env=dict(os.environ,BTS_HOST_EXECUTABLE=str(out/'bts-host'),BTS_ROOT=str(build.parent.parent))
 subprocess.run(['python3',str(root/'scripts/host/verify-native-material-gpu.py'),'--output',str(out/'gpu'),'--host-capture','--timeout','600'],env=env,check=True)
