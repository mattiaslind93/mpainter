"""Apply the reviewed host patch series to the pinned local Blender checkout.

Replay overlapping patches in a private file tree before touching the checkout.
Checking each patch backwards in the live tree is insufficient once a later patch
changes its context. Only an exact, verified series prefix may be extended.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('source',type=Path)
args=parser.parse_args()
project=Path(__file__).resolve().parents[2]
pin='fbe6228777e7d9afefcd61a413844e790ae75db7'

def git(*command,check=True):
    return subprocess.run(['git','-C',str(args.source),*command],check=check,capture_output=True,text=True)

if git('rev-parse','HEAD').stdout.strip()!=pin:
    raise SystemExit('Refusing to apply the host patches to a different Blender revision')
patches=sorted((project/'native/patches').glob('*.patch'))
names=set()
for patch in patches:
    for line in patch.read_text().splitlines():
        if line.startswith(('--- a/','+++ b/')):names.add(line[6:].split('\t',1)[0])

def contents(path):
    return path.read_bytes() if path.is_file() else None

actual={name:contents(args.source/name) for name in names}
build=Path(os.environ.get('BTS_ROOT',args.source.parent))/'build'
build.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fork-patch-check-',dir=build) as directory:
    tree=Path(directory)
    for name in names:
        result=subprocess.run(['git','-C',str(args.source),'show','HEAD:'+name],capture_output=True)
        if result.returncode==0:
            file=tree/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(result.stdout)
    matched=0 if all(contents(tree/name)==actual[name] for name in names) else None
    for index,patch in enumerate(patches,1):
        result=subprocess.run(['git','apply',str(patch)],cwd=tree,capture_output=True,text=True)
        if result.returncode:raise SystemExit(f'Invalid patch series at {patch.name}:\n{result.stderr}')
        if all(contents(tree/name)==actual[name] for name in names):matched=index
    if matched is None:raise SystemExit('Checkout differs from every reviewed patch-series prefix; preserving local changes and refusing to apply patches')
for index,patch in enumerate(patches):
    if index<matched:print('Already applied:',patch.name)
    else:git('apply',str(patch));print('Applied:',patch.name)
