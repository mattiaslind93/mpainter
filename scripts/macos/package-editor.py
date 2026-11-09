#!/usr/bin/env python3
"""Create a local, unnotarized DMG only from an audited and tested arm64 app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--app',type=Path,required=True)
p.add_argument('--proof',type=Path,action='append',required=True,help='Bundled run-editor-test result.json')
p.add_argument('--evidence',type=Path,required=True,help='Independent verify-editor-evidence result')
p.add_argument('--closure',type=Path,required=True,help='Bundle dependency/architecture audit')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();work=Path(__file__).resolve().parents[3]
app=a.app.resolve();out=a.output.resolve();exe=app/'Contents/MacOS/mPainter'
if not app.is_relative_to(work) or not out.is_relative_to(work):p.error('Keep bundle and image inside mpainter-build')
if out.exists() or Path(str(out)+'.sha256').exists():p.error('Preserve existing artifacts; use a new output path')
priority=os.getpriority(os.PRIO_PROCESS,0)
if priority<15:os.nice(15-priority)
os.environ.update(GIT_CONFIG_GLOBAL=str(work/'toolchains/gitconfig'),GIT_CONFIG_NOSYSTEM='1',TMPDIR=str(work/'tmp'))
with exe.open('rb') as f:binary_hash=hashlib.file_digest(f,'sha256').hexdigest()
for path in a.proof:
 data=json.loads(path.read_text())
 if not data['passed'] or not data['bundled_resources'] or data['binary_sha256']!=binary_hash:
  p.error('Proof does not match this tested bundle: '+str(path))
 if Path(data['command'][0]).resolve()!=exe:p.error('Test used another app location: '+str(path))
closure=json.loads(a.closure.read_text())
assert not closure['unresolved'] and all('arm64' in arch.split() for arch in closure['architectures'].values())
evidence=json.loads(a.evidence.read_text())
assert evidence['paint']['stored_gpu_second_save_open_bit_exact']
assert evidence['layers']['cpu_gpu_reference_equal'] and evidence['layers']['operations']==23
assert evidence['bake']['undo_redo_save_open'] and evidence['export']['four_real_encodings']
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
# Keep the image source separate from the destination; never mount the image.
stage=Path(tempfile.mkdtemp(prefix='mpainter-dmg-',dir=work/'tmp'))
subprocess.run(['/bin/cp','-cR',str(app),str(stage/'mPainter.app')],check=True)
(stage/'README.txt').write_text('mPainter 0.9.0-alpha, macOS arm64\nLocal Metal alpha. No Developer ID signature or notarization.\nPresentation uses synchronous Metal/QRhi capture and raster overlays.\nValidated offscreen only; no manual visible-window or older-macOS testing.\n')
out.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['hdiutil','create','-volname','mPainter 0.9.0-alpha','-srcfolder',str(stage),'-format','UDZO','-fs','HFS+','-imagekey','zlib-level=6',str(out)],check=True)
subprocess.run(['hdiutil','verify',str(out)],check=True)
with out.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
Path(str(out)+'.sha256').write_text(digest+'  '+out.name+'\n')
manifest={'path':str(out),'bytes':out.stat().st_size,'sha256':digest,'binary_sha256':binary_hash,'proofs':[str(x.resolve()) for x in a.proof],'evidence':str(a.evidence.resolve()),'closure':str(a.closure.resolve()),'source_retained':str(stage),'developer_id_signed':False,'notarized':False,'local_adhoc_code_hashes':True,'mounted':False}
Path(str(out)+'.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
