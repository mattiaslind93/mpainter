#!/usr/bin/env python3
"""Wrap the staged Linux package with pinned, checksum-verified AppImage tools."""
import argparse,hashlib,os,pathlib,subprocess,urllib.request
import shutil, sys, tempfile
p=argparse.ArgumentParser();p.add_argument('--appdir',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--cache',required=True,type=pathlib.Path);a=p.parse_args()
# package-linux.py names the staged AppDir after the version in pyproject.toml.
# A caller still asking for last release's name would stop here on a missing
# directory, so the staged AppDir is the source of truth: take its version into
# the package name instead of stamping a stale one on the finished AppImage.
if not a.appdir.is_dir():
    staged=sorted(a.appdir.parent.glob('mPainter-*-linux-x86_64'))
    if len(staged)!=1:raise SystemExit('No single staged AppDir beside '+str(a.appdir))
    a.appdir=staged[0];a.output=a.output.with_name(staged[0].name+'.AppImage')
subprocess.run([sys.executable,str(pathlib.Path(__file__).absolute().with_name("check-environment.py")),str(a.appdir)],check=True)
a.cache.mkdir(parents=True,exist_ok=True);a.output.parent.mkdir(parents=True,exist_ok=True)
# Embedded Python may ignore PYTHONDONTWRITEBYTECODE during a staging launch test.
# Bytecode caches can contain absolute build paths and are never release inputs.
for cache_dir in list(a.appdir.rglob('__pycache__')):shutil.rmtree(cache_dir)
for bytecode in a.appdir.rglob('*.pyc'):bytecode.unlink()
assets=[('appimagetool-x86_64.AppImage','https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage','ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0'),('runtime-x86_64','https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64','2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d')]
for name,url,digest in assets:
    target=a.cache/name
    if not target.exists():urllib.request.urlretrieve(url,target)
    if hashlib.sha256(target.read_bytes()).hexdigest()!=digest:raise SystemExit('Checksum mismatch: '+name)
    # The verified cache may be shared and owned by another user; only fix a missing exec bit.
    if not os.access(target,os.X_OK):target.chmod(0o755)
for name,target in [('AppRun','mpainter'),('.DirIcon','se.mattiaslind.mpainter.png')]:
    path=a.appdir/name
    if not path.exists():path.symlink_to(target)
license_file=a.cache/'LICENSE-runtime'
if not license_file.exists():urllib.request.urlretrieve('https://raw.githubusercontent.com/AppImage/type2-runtime/20251108/LICENSE',license_file)
(a.appdir/'licenses').mkdir(exist_ok=True)
shutil.copy2(license_file,a.appdir/'licenses/AppImage-runtime-LICENSE')
# Metadata belongs in the standard AppDir share path as well as the portable package root.
metadata=a.appdir/'usr/share/metainfo';metadata.mkdir(parents=True,exist_ok=True)
shutil.copy2(a.appdir/'se.mattiaslind.mpainter.metainfo.xml',metadata/'se.mattiaslind.mpainter.appdata.xml')
applications=a.appdir/'usr/share/applications';applications.mkdir(parents=True,exist_ok=True)
shutil.copy2(a.appdir/'se.mattiaslind.mpainter.desktop',applications)
# Approved artwork may be read-only; replace generated copies when repackaging.
def copy_icon(source,target):
    target.unlink(missing_ok=True)
    shutil.copy2(source,target)
for size in (16,24,32,48,64,128,256,512,1024):
    icons=a.appdir/f'usr/share/icons/hicolor/{size}x{size}/apps';icons.mkdir(parents=True,exist_ok=True)
    copy_icon(a.appdir/f'branding/mpainter-icon-{size}.png',icons/'se.mattiaslind.mpainter.png')
icons=a.appdir/'usr/share/icons/hicolor/scalable/apps';icons.mkdir(parents=True,exist_ok=True)
copy_icon(a.appdir/'branding/mpainter.svg',icons/'se.mattiaslind.mpainter.svg')
binary=a.appdir/'usr/bin/mpainter';binary.parent.mkdir(parents=True,exist_ok=True)
if not binary.exists():binary.symlink_to('../../mpainter')
# Build beside the destination, then replace it atomically. A FUSE runtime can
# still hold the previous AppImage open after its application window closes.
# Failed builds must leave the last usable artifact intact.
with tempfile.TemporaryDirectory(prefix='.mpainter-appimage-',dir=a.output.parent) as temp:
    scratch=pathlib.Path(temp)/'tmp';scratch.mkdir()
    env=dict(os.environ,ARCH='x86_64',TMPDIR=str(scratch),XDG_CACHE_HOME=str(scratch))
    candidate=pathlib.Path(temp)/a.output.name
    subprocess.run([str((a.cache/assets[0][0]).absolute()),'--appimage-extract-and-run','--runtime-file',str((a.cache/assets[1][0]).absolute()),'--comp','zstd','--mksquashfs-opt','-Xcompression-level','--mksquashfs-opt','5','--mksquashfs-opt','-processors','--mksquashfs-opt','2',str(a.appdir.absolute()),str(candidate.absolute())],env=env,check=True)
    candidate.chmod(0o755)
    candidate.replace(a.output)
