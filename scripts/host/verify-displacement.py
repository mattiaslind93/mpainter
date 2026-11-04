"""Real File-New Displacement/Bump preview, original-source invariants and Save/Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path);p.add_argument('--resolution',type=int,default=512);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_DISPLACEMENT_DEMO='1',BTS_LEGACY_DISPLACEMENT_PROJECT=str(root/'fixtures/legacy/schema45-painted.blend'))
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(a.mesh or root/'fixtures/single_tile.obj'),'--gui-fix-resolution',str(a.resolution),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'displacement.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
assert not c['created']['displacement']['allocated'] and not c['created']['displacement']['channel']
assert c['bump-painted']['bump_hash']!=c['paint-empty']['bump_hash']
assert c['bump-painted']['geometry']['objects']==c['paint-empty']['geometry']['objects']
assert c['disp-painted-level0']['displacement_pages']>0
assert c['disp-painted-level0']['displacement_hash']!=c['disp-empty']['displacement_hash']
for stage in ['disp-painted-level0','midlevel-zero','midlevel-one','disp-level2','undo-enabled','saved','opened','undo-defaults','restored-defaults']:
 materials=c[stage]['geometry']['materials'];assert materials and any(m['method']==2 and m['evaluated_method']==2 and m['linked'] and m['content'] for m in materials),(stage,materials)
for stage in ['strength-zero','disabled-retained','redo-disabled','channel-disabled-retained']:
 assert all(m['method']==0 and m['evaluated_method']==0 and not m['linked'] for m in c[stage]['geometry']['materials']),stage
assert c['restored-defaults']['displacement']['strength']==1
assert c['disp-level2']['displacement']['subdivision']==2
assert any(o['evaluated_vertices']>o['source_vertices'] and o['level']==2 for o in c['disp-level2']['geometry']['objects'])
assert c['disp-level2']['displacement']['mode']==0 and c['catmull-clark']['displacement']['mode']==2
for stage,kind in [('disp-level2',1),('catmull-clark',0),('undo-catmull',1),('redo-catmull',0),('opened',0)]:
 objects=c[stage]['geometry']['objects'];active=[o for o in objects if o['preview_modifiers']]
 assert active and all(o['subdivision_type']==kind and o['uv_smooth']==0 and o['quality']==3 and o['use_creases'] and o['boundary_smooth']==0 for o in active),stage
 assert any(o['evaluated_vertices']>o['source_vertices'] for o in active),stage
 for original,current in zip(c['paint-empty']['geometry']['objects'],objects):assert original['source_uv_hash']==current['source_uv_hash'],stage
assert [o['evaluated_geometry_hash'] for o in c['disp-level2']['geometry']['objects']]!=[o['evaluated_geometry_hash'] for o in c['catmull-clark']['geometry']['objects']]
assert c['undo-catmull']['geometry']['objects']==c['disp-level2']['geometry']['objects']
assert c['redo-catmull']['geometry']['objects']==c['catmull-clark']['geometry']['objects']
assert c['restored-defaults']['displacement']['mode']==0
for stage in ['uv-uniform','uv-catmull']:
 assert ImageChops.difference(Image.open(a.output/'uv-level0.png').convert('RGB'),Image.open(a.output/(stage+'.png')).convert('RGB')).getbbox() is None,stage
assert all((a.output/(stage+'.obj')).read_bytes()==(a.output/'paint-empty.obj').read_bytes() for stage in ['catmull-clark','undo-catmull','redo-catmull'])
for stage in ['catmull-clark','undo-catmull','redo-catmull']:
 assert c[stage]['source_mesh_hash']==c['paint-empty']['source_mesh_hash'] and c[stage]['picked_uv']==c['paint-empty']['picked_uv'],stage
assert all(o['preview_modifiers']==0 for o in c['disabled-retained']['geometry']['objects'])
assert c['disabled-retained']['displacement']['strength']==c['disp-level2']['displacement']['strength']
for stage in ['bump-painted','disp-empty','disp-painted-level0','strength-zero','midlevel-zero','midlevel-one','disp-level2','disabled-retained','undo-enabled','saved','opened']:
 assert c[stage]['source_mesh_hash']==c['paint-empty']['source_mesh_hash'],stage
 assert c[stage]['picked_uv']==c['paint-empty']['picked_uv'],stage
assert all((a.output/(stage+'.obj')).read_bytes()==(a.output/'paint-empty.obj').read_bytes() for stage in ['bump-painted','disp-painted-level0','fill-level0','disp-level2','disabled-retained','opened'])
assert c['saved']['displacement']==c['opened']['displacement']
assert c['saved']['displacement_hash']==c['opened']['displacement_hash']
assert c['saved']['bump_hash']==c['opened']['bump_hash']
assert not c['legacy']['displacement']['channel'] and not c['legacy']['displacement']['allocated']
assert any(t['hashes'][4]=='f8cea457dd723e79' for s in c['legacy']['document']['texture_sets'] for l in s['layers'] for t in l['paint_tiles'])
def delta(first,second):
 rect=next(p['rect'] for p in c[first]['view']['panes'] if p['view']=='3D');x,y,w,h=map(int,rect);box=(x+10,y+40,x+w-10,y+h-30)
 one=Image.open(a.output/(first+'.png')).convert('RGB').crop(box);two=Image.open(a.output/(second+'.png')).convert('RGB').crop(box)
 return sum(ImageStat.Stat(ImageChops.difference(one,two)).mean)/(3*255)
differences={f'{first}/{second}':delta(first,second) for first,second in [('paint-empty','bump-painted'),('fill-level0','strength-zero'),('midlevel-zero','midlevel-one'),('fill-level0','disp-level2'),('disp-level2','catmull-clark')]}
assert min(differences.values())>1e-5,differences
assert differences['fill-level0/strength-zero']>.001,differences
outputs=list((a.output/'gui-export').glob('*.png'));assert outputs
assert any(p.name.endswith('_bump.png') for p in outputs) and any(p.name.endswith('_displacement.png') for p in outputs)
for suffix,value in [('bump',0),('displacement',255)]:
 image=Image.open(next(p for p in outputs if p.name.endswith('_'+suffix+'.png'))).convert('L');assert abs(image.getpixel((image.width//2,image.height//2))-value)<=1
for output in outputs:
 suffix=output.stem.split('_',1)[1];assert suffix==suffix.lower() and 'Height' not in suffix and '_Bump' not in output.name
for stage in c:
 im=Image.open(a.output/(stage+'.png')).convert('RGB');assert not any(r>240 and g<15 and b>240 for r,g,b in im.getdata()),stage
assert all(v['decimal']=='.' and not v['shader_failures'] for v in c.values())
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',bump_shading_only=True,displacement_method='BOTH',independent_stroke=True,preview_subdivision=True,uniform_simple_and_catmull_clark=True,uv_no_smoothing_identical=True,source_geometry_uv_and_picking_retained=True,save_open=True,legacy_height_hash_preserved=True,image_differences=differences,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
