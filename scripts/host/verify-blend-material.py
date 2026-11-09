# SPDX-License-Identifier: GPL-3.0-or-later
"""Native explicit material library -> Shelf -> GPU Fill and source-free packed Open."""
from parity_test_environment import private_display
import argparse,hashlib,json,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));fixtures=a.output/'fixtures'
with (a.output/'fixtures.log').open('w')as log:
 subprocess.run([str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(root/'scripts/host/generate-blend-material-fixture.py'),'--',str(fixtures)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
source=fixtures/'materials.blend';before=hashlib.sha256(source.read_bytes()).hexdigest()
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_BLEND_MATERIAL_DEMO='1',BTS_BLEND_MATERIAL_SOURCE=str(source),BTS_SKEW_DEMO='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
try:
 with (a.output/'run.log').open('w')as log:
  subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
finally:
 held=Path(str(source)+'.held')
 if held.exists():held.rename(source)
d=json.loads((a.output/'blend-material.json').read_text());c={s['stage']:s for s in d['captures']}
assert len(c)==9,c.keys()
assert c['before-import']['document']==c['shelf-imported']['document']
assert len(c['shelf-imported']['blend_materials'])==1
asset=c['shelf-imported']['blend_materials'][0];v=asset['material']
assert asset['path']=='' and asset['scope']=='session' and asset['descriptor']['marked_asset'] and d['saved_preview']
assert abs(v['metallic']-.8)<1e-6 and abs(v['roughness']-.23)<1e-6
# Reference: linear Rec.709 -> ACEScg (AP1 / D60), no display gamma.
matrix=[[.6130974024,.3395231462,.0473794514],[.0701937225,.9163538791,.0134523985],[.0206155929,.1095697729,.8698146342]]
expected=[sum(row[i]*[.6,.1,.025][i]for i in range(3))for row in matrix]
assert max(abs(x-y)for x,y in zip(v['base_color'][:3],expected))<2e-4,(v,expected)
sets=c['fill-applied']['document']['texture_sets'];layers=sets[0]['layers'];assert len(layers)==2
fill=layers[-1];assert fill['name']=='Copper constant' and fill['channels']==[True,True,True,False,False]
# Store schema-native result, check whole-document history and persistence.
assert c['undone']['document']==c['before-import']['document']
assert c['drop-undone']['document']==c['redone']['document']
assert len(c['dropped']['document']['texture_sets'][0]['layers'])==3
assert c['redone']['document']==c['fill-applied']['document']==c['opened-without-source']['document']==c['unsupported-unchanged']['document']
def quantized(x,precision):
 if precision==0:
  encoded=12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055
  encoded=int(encoded*255+.5)/255
  return encoded/12.92 if encoded<=.04045 else ((encoded+.055)/1.055)**2.4
 if precision in [1,2]:return int(x*255+.5)/255
 if precision in [3,4]:return int(x*65535+.5)/65535
 return x
def gpu_value(x,precision):
 value=quantized(x,precision)
 return value if precision in [7,8]else struct.unpack("e",struct.pack("e",value))[0]
for stage in ['fill-applied' ,'redone','opened-without-source','dropped']:
 samples=c[stage]['material_gpu_samples'];assert all(samples),samples
 precision=c[stage]['document']['texture_sets'][0]['precision']
 assert max(abs(samples[0][i]-gpu_value(v['base_color'][i],precision[0]))for i in range(3))<3e-6,(stage,samples,v)
 assert abs(samples[1][0]-gpu_value(v['metallic'],precision[1]))<3e-6 and abs(samples[2][0]-gpu_value(v['roughness'],precision[2]))<3e-6
project=[x for x in c['opened-without-source']['blend_materials']if x['scope']=='project'];assert len(project)==1 and project[0]['material']==v
assert not (a.output/'library'/'library.json').exists()
assert hashlib.sha256(source.read_bytes()).hexdigest()==before
log=(a.output/'run.log').read_text();assert f'BTS_GPU_READY pid={d["pid"]}'in log and f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
assert 'Not freed memory blocks'not in log
assert not re.search(r'BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'pid':d['pid'],'explicit_source_native_same_process':True,'principled_constants_acescg_and_raw_scalars':True,'marked_asset_and_saved_preview':True,'shelf_fill_history':True,'packed_open_without_library':True,'unsupported_link_visible_and_atomic':d['rejected'],'no_default_library_write':True,'source_unchanged':True,'native_gpu_channel_pixels':True,'shelf_drag_and_drop':True,'captures':list(c)}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
