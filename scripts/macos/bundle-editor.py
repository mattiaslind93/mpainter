#!/usr/bin/env python3
"""Assemble and audit an arm64 editor bundle locally. No signing identity/network."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--resume',action='store_true',help='Refresh a workspace staging bundle after a build/deployment fix')
p.add_argument('--finish',action='store_true',help='Resume dependency audit/signing without recopying or redeploying Qt')
a=p.parse_args()
a.resume=a.resume or a.finish
work=Path(__file__).resolve().parents[3]
repo=work/'mpainter'
app=a.output.resolve()
if not app.is_relative_to(work) or app.suffix!='.app':p.error('Use a workspace-local .app path')
if app==work/'build-metal/bin/mPainter.app':p.error('Do not package in the build source bundle')
if app.exists() and not a.resume:p.error('Destination exists; use a new path or --resume for staging')
if a.resume and not (app/'Contents/Resources/5.2').is_dir():p.error('Resume requires a previously assembled staging bundle')
priority=os.getpriority(os.PRIO_PROCESS,0)
if priority<15:os.nice(15-priority)
os.environ.update(GIT_CONFIG_GLOBAL=str(work/'toolchains/gitconfig'),GIT_CONFIG_NOSYSTEM='1',TMPDIR=str(work/'tmp'))
def run(*args):
 try:return subprocess.check_output([str(v) for v in args],text=True,stderr=subprocess.STDOUT)
 except subprocess.CalledProcessError as error:
  print(error.output,flush=True)
  raise
contents=app/'Contents';resources=contents/'Resources';macos=contents/'MacOS';frameworks=contents/'Frameworks'
if not a.resume:
 shutil.copytree(work/'build-metal/bin/mPainter.app',app,symlinks=True)
 frameworks.mkdir(exist_ok=True)
 runtime=work/'build-metal/runtime/Blender.app/Contents/Resources'
 for name in ['5.2','lib']:
  shutil.copytree(runtime/name,resources/name,symlinks=True)
  (macos/name).symlink_to('../Resources/'+name,target_is_directory=True)
 shutil.copytree(repo/'assets',resources/'assets',symlinks=True)
 (macos/'assets').symlink_to('../Resources/assets',target_is_directory=True)
 # Current resource readers use applicationDirPath; these relative links remain
 # valid after moving the bundle and contain no build-machine prefixes.
 if not (macos/'branding').exists():(macos/'branding').symlink_to('../Resources/assets/branding',target_is_directory=True)
 for name in ['default-studio.hdr','default-studio.txt']:
  shutil.copy2(repo/'assets/environments'/name,resources/'5.2/datafiles/studiolights/world'/name)
 # This native editor has no Python application package; Blender's embedded
 # Python and scripts are already included above.
 shutil.copy2(repo/'LICENSE',resources/'mPainter-LICENSE.txt')
 # Same top-level texts, license notes and BUILD-INFO as the Linux and Windows packages,
 # so scripts/license-gate.py can gate the bundle (pass it the .app).
 for name in ['LICENSE','NOTICE.md','README.md']:shutil.copy2(repo/name,resources/name)
 licenses=resources/'licenses';licenses.mkdir(exist_ok=True)
 for source,target in [('assets/grunges/README.md','mask-grunges.md'),('assets/mask-preview/README.md','mask-preview.md'),('assets/meshes/Shader_Ball_JL_01-NOTICE.md','shaderball-jl01.md'),('assets/meshes/CC-BY-4.0.txt','CC-BY-4.0.txt')]:
  shutil.copy2(repo/source,licenses/target)
 shutil.copytree(repo/'assets/file-browser',licenses/'file-browser',ignore=shutil.ignore_patterns('atlas.png','*.qrc','manifest.json','svg'))
 # The aqt macOS archive may omit Qt's license texts; any Qt 6.8.3 install's collection is the same.
 qt_licenses=next((p for p in [work/'toolchains/qt/6.8.3/macos/licenses',work/'toolchains/qt/licenses'] if p.is_dir()),None)
 if qt_licenses is None:raise SystemExit('No Qt 6.8.3 license collection found for licenses/Qt')
 shutil.copytree(qt_licenses,licenses/'Qt/licenses')
 for name in ['license','licenses']:
  if (runtime/name).is_dir():shutil.copytree(runtime/name,licenses/'Blender',dirs_exist_ok=True)
 version=re.search(r'^version = "([^"]+)"',(repo/'pyproject.toml').read_text(),re.M).group(1)
 commit=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
 (resources/'BUILD-INFO.txt').write_text(f'mPainter {version}\nCommit {commit}\nBlender 5.2.0\nQt 6.8.3\nmacOS arm64, Metal backend.\nHTTPS uses the Qt TLS plugin in Contents/PlugIns/tls with the macOS TLS stack.\nNo Developer ID signature or notarization.\nSource preparation and package commands: docs/building.md in the matching source tree.\n')
elif not a.finish:
 shutil.copy2(work/'build-metal/bin/mPainter.app/Contents/MacOS/mPainter',macos/'mPainter')
# Data belongs in Resources. Retain the build's duplicate branding directory
# outside the app and use a relocatable resource link for existing readers.
branding=macos/'branding'
if branding.is_dir() and not branding.is_symlink():
 retained=app.parent/(app.stem+'-build-branding')
 if retained.exists():raise RuntimeError('Branding backup exists; preserve it and use a fresh staging directory')
 shutil.move(branding,retained)
 branding.symlink_to('../Resources/assets/branding',target_is_directory=True)
# macdeployqt handles framework metadata and Qt platform/image plugins.
deploy=work/'toolchains/qt/6.8.3/macos/bin/macdeployqt'
if not a.finish:print(run(deploy,app,'-no-strip','-verbose=1'),flush=True)
# macdeployqt deploys Cocoa only; retain the explicit offscreen test platform.
platforms=contents/'PlugIns/platforms';platforms.mkdir(parents=True,exist_ok=True)
offscreen=platforms/'libqoffscreen.dylib'
if not offscreen.exists():shutil.copy2(work/'toolchains/qt/6.8.3/macos/plugins/platforms/libqoffscreen.dylib',offscreen)
# The update check needs HTTPS. Do not rely on macdeployqt's plugin choice: require a
# backend that uses the macOS TLS stack, which needs no bundled OpenSSL.
tls=contents/'PlugIns/tls';tls.mkdir(parents=True,exist_ok=True)
secure=tls/'libqsecuretransportbackend.dylib'
if not secure.exists():shutil.copy2(work/'toolchains/qt/6.8.3/macos/plugins/tls'/secure.name,secure)
(resources/'qt.conf').write_text('[Paths]\nPlugins = PlugIns\n')
# Qt resolves qt.conf from Contents/Resources on macOS, Plugins relative to Contents.
magic={b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca'}
def macho(path):
 if not path.is_file() or path.is_symlink():return False
 with path.open('rb') as f:header=f.read(16)
 if header[:4] not in magic:return False
 # Embedded Python ships linker objects too; MH_OBJECT files are not loaded.
 if header[:4]==b'\xcf\xfa\xed\xfe' and struct.unpack('<I',header[12:16])[0]==1:return False
 if header[:4]==b'\xfe\xed\xfa\xcf' and struct.unpack('>I',header[12:16])[0]==1:return False
 return True
def deps(path):
 # otool -L also prints LC_ID_DYLIB. That is the library's identity, not a
 # dependency that must resolve through its own rpath search list.
 own=run('otool','-D',path).splitlines()[1:]
 return [line.strip().split(' (compatibility version')[0] for line in run('otool','-L',path).splitlines()[1:] if 'compatibility version' in line and line.strip().split(' (compatibility version')[0] not in own]
def rpaths(path):
 lines=run('otool','-l',path).splitlines();result=[]
 for i,line in enumerate(lines):
  if line.strip()=='cmd LC_RPATH':result.append(lines[i+2].strip().split('path ',1)[1].rsplit(' (offset ',1)[0])
 return result
def system(path):return path.startswith(('/System/Library/','/usr/lib/'))
# Include embedded Python extensions and Blender's runtime dylibs, not only the
# main executable. Copy and rewrite any remaining non-system absolute imports.
# OIDN discovers these modules dynamically beside its core; otool cannot see
# them as ordinary imports. macdeployqt copies the core into Frameworks.
for plugin in (resources/'lib').glob('libOpenImageDenoise_device_*.dylib'):
 target=frameworks/plugin.name
 if not target.exists():shutil.copy2(plugin,target)
queue=[path for path in contents.rglob('*') if macho(path)];seen=set();changed=[]
while queue:
 binary=queue.pop()
 if binary in seen:continue
 seen.add(binary)
 edits=[]
 for dep in deps(binary):
  if not dep.startswith('/') or system(dep):continue
  source=Path(dep)
  if source.is_relative_to(app):continue
  if not source.is_file():raise RuntimeError(f'Missing dependency: {binary}: {dep}')
  if not source.resolve().is_relative_to(work):raise RuntimeError(f'Non-workspace dependency: {dep}')
  dest=frameworks/source.name
  if not dest.exists():shutil.copy2(source,dest);queue.append(dest)
  edits+=['-change',dep,'@rpath/'+dest.name]
 paths=rpaths(binary)
 for path in paths:
  if path.startswith('/') and not system(path):edits+=['-delete_rpath',path]
 for target in [binary.parent,frameworks,resources/'lib']:
  path='@loader_path/'+os.path.relpath(target,binary.parent)
  # Resources/lib is also binary.parent there; add each rpath only once.
  if path not in paths:edits+=['-add_rpath',path];paths.append(path)
 if binary.suffix in ('.dylib','.so'):
  # Install IDs are metadata, and must not retain the build's absolute prefix.
  edits+=['-id','@rpath/'+binary.name]
 if edits:
  run('install_name_tool',*edits,binary);changed.append(str(binary.relative_to(app)))
# Resolve every load command against loader rpaths and the main executable's
# search paths. This is structural closure; the separate offscreen run proves loading.
exe=macos/'mPainter'
def expand(value,binary):
 return value.replace('@loader_path',str(binary.parent)).replace('@executable_path',str(macos))
failures=[];architectures={}
for link in contents.rglob('*'):
 if link.is_symlink() and (not link.exists() or not link.resolve().is_relative_to(app)):
  failures.append('Broken or external resource symlink: '+str(link.relative_to(app)))
main_rpaths=rpaths(exe)
for binary in sorted(seen):
 arch=run('lipo','-archs',binary).strip();architectures[str(binary.relative_to(app))]=arch
 if 'arm64' not in arch.split():failures.append(f'No arm64 slice: {binary}')
 search=rpaths(binary)+main_rpaths
 for dep in deps(binary):
  if system(dep):continue
  if dep.startswith('@rpath/'):
   candidates=[Path(expand(r,binary))/dep[len('@rpath/'):] for r in search]
  else:candidates=[Path(expand(dep,binary))]
  if not any(x.exists() and x.resolve().is_relative_to(app) for x in candidates):failures.append(f'{binary.relative_to(app)} -> {dep}')
report={'bundle':str(app),'macho_count':len(seen),'architectures':architectures,'rewritten':changed,'unresolved':failures,'developer_id_signed':False}
(app.parent/(app.stem+'-closure.json')).write_text(json.dumps(report,indent=2)+'\n')
if failures:raise RuntimeError('\n'.join(failures))
# Apple Silicon needs valid local code hashes after load-command edits. Ad-hoc
# signatures use no certificate, account, timestamp service or notarization.
for binary in sorted(seen,key=lambda p:len(p.parts),reverse=True):
 if binary!=exe:run('codesign','--force','--sign','-','--timestamp=none',binary)
run('codesign','--force','--deep','--sign','-','--timestamp=none',app)
run('codesign','--verify','--deep','--strict',app)
print(json.dumps({'bundle':str(app),'macho_count':len(seen),'unresolved':len(failures)}),flush=True)
