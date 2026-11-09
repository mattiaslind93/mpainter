# SPDX-License-Identifier: GPL-3.0-or-later
"""Private CPU native index/cache + six explicit-library input regressions.
Run inside the build container, with nice -n19. Does not create a GL context.
"""
import argparse,json,os,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cases',type=Path,required=True);p.add_argument('--syntax-main',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];build=a.build_dir.resolve();out=a.output.resolve();assert out.is_relative_to(root.parent/'build');out.mkdir(parents=True,exist_ok=True)
commands=subprocess.check_output([shutil.which('ninja')or'ninja-build','-t','commands','bts-host'],cwd=build,text=True).splitlines()
base=shlex.split(next(c for c in commands if '/main.cc.o' in c and ' -c ' in c));mainobj=base[base.index('-o')+1];objects=[]
for source in (['native/host/main.cc']if a.syntax_main else[])+['native/tests/blend_library_native_test.cc','native/host/BlendAssetLibrary.cc']:
 c=shlex.split(next((v for v in commands if '/'+Path(source).name+'.o' in v and ' -c ' in v),shlex.join(base)));obj=str(out/(Path(source).stem+'.o'));objects.extend([]if source.endswith('/main.cc')else[obj]);c[c.index('-o')+1]=obj;c[c.index('-c')+1]=str(root/source)
 for flag in ['-MF','-MT']:
  if flag in c:c[c.index(flag)+1]=obj+('.d' if flag=='-MF' else '')
 c.insert(2 if Path(c[0]).name=='ccache' else 1,'-I'+str(root/'native/host'));c+=['-UNDEBUG']+(['-fsyntax-only']if source.endswith('/main.cc')else[]);print('Compile',source,flush=True);subprocess.run(c,cwd=build,check=True)
link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link
if '&&'in link:link=link[:link.index('&&')]
link=[v for v in link if v!=mainobj and not v.endswith('/BlendAssetLibrary.cc.o')];link[1:1]=objects;link[link.index('-o')+1]=str(out/'library-native-test');link=['-Wl,--dependency-file='+str(out/'link.d')if v.startswith('-Wl,--dependency-file=')else v for v in link];print('Link private native proof',flush=True);subprocess.run(link,cwd=build,check=True)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',OCIO=str(build.parent.parent/'blender/release/datafiles/colormanagement/config.ocio'))
for key,name in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_BLEND_LIBRARY_CACHE','indices')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
if not(out/'5.2').exists():subprocess.run(['cp','-a','--reflink=auto',str(build/'bin/5.2'),str(out/'5.2')],check=True)
env.update(BLENDER_SYSTEM_RESOURCES=str(out/'5.2'),BLENDER_SYSTEM_PYTHON=str(out/'5.2/python'),BLENDER_SYSTEM_DATAFILES=str(out/'5.2/datafiles'),PYTHONDONTWRITEBYTECODE='1')
for line in (build/'CMakeCache.txt').read_text().splitlines():
 if line.startswith('PYTHON_EXECUTABLE:FILEPATH='):env['PYTHONHOME']=str(Path(line.split('=',1)[1]).parent.parent)
sdk=build.parent.parent/'qt/6.8.3';env.update(QT_PLUGIN_PATH=str(sdk/'plugins'),LD_LIBRARY_PATH=str(sdk/'lib')+(':'+env['LD_LIBRARY_PATH']if env.get('LD_LIBRARY_PATH')else''))
with(out/'run.log').open('w')as log:subprocess.run([str(out/'library-native-test'),str(out),str(a.cases.resolve())],cwd=build,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
print((out/'library-index.json').read_text())
