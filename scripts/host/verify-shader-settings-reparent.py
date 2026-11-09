# SPDX-License-Identifier: GPL-3.0-or-later
"""CPU-only real ShaderController/WorkspaceShell ownership regression.

Use an immutable native-ABI-compatible host snapshot. Only ShaderController and
this test are compiled; all other native/vendor inputs retain snapshot identity.
Run in the build container at nice19; never reads mutable artist .o/.a inputs.
"""
import argparse,json,os,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host-snapshot',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--baseline',action='store_true',help='Run unmodified snapshot controller; expected to reject the regression fixture');a=p.parse_args()
repo=Path(__file__).resolve().parents[2];snapshot=a.host_snapshot.resolve();out=a.output.resolve()
assert out.is_relative_to(repo.parent/'build') and not out.is_relative_to(snapshot) and not out.is_relative_to(repo.parent/'build/host')
out.mkdir(parents=True,exist_ok=True);commands=json.loads((snapshot/'host-commands.json').read_text());source_head=json.loads((snapshot/'snapshot.json').read_text())['source_head']
for header in ['ShaderController.hh','WorkspaceShell.hh','TextureDocument.hh','NativeMaterialParameters.hh','LayerEffect.hh','MaskEffect.hh','AssetCatalog.hh']:
 assert subprocess.check_output(['git','show',source_head+':native/host/'+header],cwd=repo)==(repo/'native/host'/header).read_bytes(),'Snapshot ABI mismatch: '+header
base=shlex.split(next(c for c in commands if '/ShaderController.cc.o -c ' in c));old_source=Path(base[base.index('-c')+1]).parents[2];old_object=base[base.index('-o')+1]
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',CCACHE_DIR=str(repo.parent/'build/ccache'))
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('TMPDIR','tmp')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
def compile(source,label):
 args=[x.replace(str(old_source/'native'),str(repo/'native'))for x in base];obj=str(out/(label+'.o'));args[args.index('-c')+1]=str(repo/source);args[args.index('-o')+1]=obj
 for option,value in [('-MF',obj+'.d'),('-MT',obj)]:
  if option in args:args[args.index(option)+1]=value
 args[2:2]=['-I'+str(repo/'native/host')];args+=['-g0']
 with(out/(label+'.compile.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return obj
controller=compile('native/host/ShaderController.cc','ShaderController')if not a.baseline else str(snapshot/old_object);test=compile('native/tests/shader_settings_reparent_test.cc','test')
link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link
if '&&'in link:link=link[:link.index('&&')]
for i,value in enumerate(link):
 if value==old_object:link[i]=controller
 elif value.endswith('/main.cc.o'):link[i]=test
 elif value.endswith(('.o','.a')) or value.startswith('bin/lib/'):
  path=Path(value);path=(path if path.is_absolute()else snapshot/path).resolve();assert path.is_file()and not path.is_relative_to(repo.parent/'build/host');link[i]=str(path)
 elif value.startswith('-Wl,--dependency-file='):link[i]='-Wl,--dependency-file='+str(out/'link.d')
 elif value.startswith('-Wl,-rpath,'):link[i]=value.replace(str(repo.parent/'build/host/bin/lib'),str(snapshot/'bin/lib'))
binary=out/'shader-settings-reparent';link[link.index('-o')+1]=str(binary)
with(out/'link.log').open('w')as log:subprocess.run(['nice','-n','19',*link],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
sdk=repo.parent/'qt/6.8.3';env.update(QT_PLUGIN_PATH=str(sdk/'plugins'),LD_LIBRARY_PATH=str(sdk/'lib')+':'+str(snapshot/'bin/lib')+':'+env.get('LD_LIBRARY_PATH',''))
with(out/'run.log').open('w')as log:result=subprocess.run(['nice','-n','19',str(binary)],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
if a.baseline:
 assert result.returncode!=0,'Unmodified controller unexpectedly accepted reparenting'
 assert 'Reparented OpenPBR sync did not hide owned legacy title' in (out/'run.log').read_text(),'Historical negative failed for an unrelated reason'
else:assert result.returncode==0,'Controller regression failed: '+str(out/'run.log')
print('Historical negative reproduced'if a.baseline else 'Shader settings reparent regression PASS')
