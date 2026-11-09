# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the export-only C++ conversion against Mattias's Blender/NumPy reference. Requires NumPy and SciPy."""
import sys,types,contextlib,io,subprocess,json,argparse,os
from pathlib import Path
import numpy as np
parser=argparse.ArgumentParser()
parser.add_argument('--reference',type=Path,required=True)
parser.add_argument('--converter',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--distrobox',default='')
args=parser.parse_args()
os.environ.update(BTS_NO_BACKGROUND_GPU='1',QT_QPA_PLATFORM='offscreen',QT_QPA_OFFSCREEN_NO_GLX='1')
ref=args.reference
class Images(dict):
 def new(self,name,width,height,**kw):
  image=types.SimpleNamespace(name=name,size=(width,height),pixels=None);self[name]=image;return image
sys.modules['bpy']=types.SimpleNamespace(data=types.SimpleNamespace(images=Images()))
ns={};exec(ref.read_text().rsplit('convert_selected_normal_map()',1)[0],ns)
exe=str(args.converter)
out=args.output;out.mkdir(parents=True,exist_ok=True)
results=[]
for w,h in [(1,1),(128,128),(37,23),(64,128),(127,61)]:
 for case in ['flat','waves','random','grazing']:
  rng=np.random.default_rng(132);y,x=np.mgrid[:h,:w];n=np.empty((h,w,3))
  if case=='flat': n[:]=[0,0,1]
  elif case=='waves': n[:,:,0]=.3*np.sin(2*np.pi*x/w);n[:,:,1]=.2*np.cos(6*np.pi*y/h);n[:,:,2]=1
  else: n=rng.normal(0,.4,(h,w,3));n[:,:,2]=1 if case=='random' else rng.uniform(-.005,.005,(h,w))
  n/=np.linalg.norm(n,axis=2,keepdims=True)
  pixels=np.ones((h,w,4),dtype=np.float32);pixels[:,:,:3]=n*.5+.5
  input=out/'input.f32';output=out/'output.f32';pixels.tofile(input)
  subprocess.run((['distrobox','enter',args.distrobox,'--'] if args.distrobox else [])+[exe,str(w),str(h),str(input),str(output)],check=True,capture_output=True)
  actual=np.fromfile(output,np.float32).reshape(h,w)
  image=types.SimpleNamespace(name='reference_normal',size=(w,h),pixels=pixels.ravel().tolist())
  with contextlib.redirect_stdout(io.StringIO()): expected=ns['hybrid_normal_to_bump'](image).pixels[::4]
  expected=np.array(expected).reshape(h,w)
  error=float(np.max(np.abs(actual-expected)))
  results.append(dict(width=w,height=h,case=case,maximum_error=error,mean=float(actual.mean())))
  assert error<2e-5,(results[-1])
(out/'comparison.json').write_text(json.dumps(dict(complete=True,tolerance=2e-5,cases=results),indent=2)+'\n')
print(json.dumps(dict(cases=len(results),maximum_error=max(r['maximum_error'] for r in results))))
