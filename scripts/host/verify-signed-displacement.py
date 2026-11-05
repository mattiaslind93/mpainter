"""Native negative/positive displacement at Midlevel 0 and Scale 1, persistence and defaults."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_DISPLACEMENT_DEMO='1',BTS_SIGNED_DISPLACEMENT_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'signed-displacement.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
assert c['created']['displacement']['strength']==1
assert c['restored-defaults']['displacement']['strength']==1
assert c['restored-defaults']['displacement']['midlevel']==.5
for stage in ['negative','positive','saved','opened']:
 assert c[stage]['displacement']['strength']==1 and c[stage]['displacement']['midlevel']==0,stage
 assert any(m['method']==2 and m['evaluated_method']==2 and m['linked'] and m['content'] for m in c[stage]['geometry']['materials']),stage
for stage in ['negative-zero','positive-zero','explicit-zero-opened']:
 assert c[stage]['displacement']['strength']==0 and all(m['method']==0 and m['evaluated_method']==0 for m in c[stage]['geometry']['materials']),stage
assert c['negative']['signed_range'][0]<-.2 and c['positive']['signed_range'][1]>.2
assert c['negative']['native_displacement'][0]<0 and c['positive']['native_displacement'][0]>0
assert c['negative']['bump_hash']==c['negative-empty']['bump_hash']==c['positive']['bump_hash']
assert c['saved']['displacement_hash']==c['opened']['displacement_hash']
assert c['saved']['document']==c['opened']['document']
assert c['saved']['native_displacement']==c['opened']['native_displacement']
assert all(v['brush_min']<=-1 and v['brush_max']>=1 and v['point_decimal'] and v['decimal']=='.' and not v['shader_failures'] for v in c.values())
def delta(first,second):
 rect=next(p['rect'] for p in c[first]['view']['panes'] if p['view']=='3D');x,y,w,h=map(int,rect);box=(x+10,y+40,x+w-10,y+h-30)
 one=Image.open(a.output/(first+'.png')).convert('RGB').crop(box);two=Image.open(a.output/(second+'.png')).convert('RGB').crop(box)
 return sum(ImageStat.Stat(ImageChops.difference(one,two)).mean)/(3*255)
differences={sign:delta(sign,sign+'-zero') for sign in ['negative','positive']};assert min(differences.values())>1e-5,differences
for stage in c:
 image=Image.open(a.output/(stage+'.png')).convert('RGB');assert not any(r>240 and g<15 and b>240 for r,g,b in image.getdata()),stage
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',default_scale=1,restore_defaults_scale=1,brush_range=[-1,1],native_signed_pixels=True,midlevel=0,eevee_method='BOTH',image_differences=differences,bump_unchanged=True,signed_save_open=True,explicit_saved_scale_zero_retained=True,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
