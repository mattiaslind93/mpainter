#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Relink a read-only host build with the HDR rotation regression driver.

Usage: BUILD_DIR OUTPUT_DIR DRIVER_SOURCE [current]
Omit current for the mfPainter reference; it cannot exercise mPainter migration.
Only OUTPUT_DIR is written. Run the resulting hdrrot-host under private Xvfb
inside bts-build, with the runtime's 5.2 directory beside it and Qt/Blender
resource paths configured. Arguments: executable mesh initial-HDR final-HDR output.
The reference's New validation requires its original HDR basename; the driver
selects the final HDR immediately afterwards, without changing its orientation.
"""
import os,sys,subprocess,shlex,pathlib
os.environ["CCACHE_DISABLE"]="1"
build,out,source=map(pathlib.Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
commands=subprocess.check_output(['ninja','-t','commands','bts-host'],cwd=build,text=True).splitlines()
c=shlex.split(next(x for x in commands if '/main.cc.o' in x and ' -c ' in x));old=c[c.index('-o')+1];obj=str(out/'driver.o');header=str(pathlib.Path(c[c.index('-c')+1]).parent)
c[c.index('-o')+1]=obj;c[c.index('-c')+1]=str(source);c+=['-I'+header]
if len(sys.argv)>4:c+=['-DMPAINTER_HDRROT']
for flag,name in [('-MF','compile.d'),('-MT','driver.o')]:
 if flag in c:c[c.index(flag)+1]=str(out/name)
l=shlex.split(commands[-1]);l=l[2:] if l[:2]==[':','&&'] else l;l=l[:l.index('&&')] if '&&' in l else l;l=[obj if x==old else '-Wl,--dependency-file='+str(out/'link.d') if x.startswith('-Wl,--dependency-file=') else x for x in l];l[l.index('-o')+1]=str(out/'hdrrot-host')
for cmd in [c,l]:subprocess.run(cmd,cwd=build,check=True)
