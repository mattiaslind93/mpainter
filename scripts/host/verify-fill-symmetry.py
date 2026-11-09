# SPDX-License-Identifier: GPL-3.0-or-later
"""PD144 real Properties/toolbar edits, native texture samples, masks and packed projects."""
from parity_test_environment import private_display
import argparse,json,os,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.check_only:
 (a.output/'plane.mtl').write_text('newmtl FixtureSurface\nKd 1 1 1\nPr 0.5\nPm 0\n')
 (a.output/'plane.obj').write_text('mtllib plane.mtl\no SymmetryPlane\nusemtl FixtureSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
 image=Image.new('RGBA',(32,32),(0,0,0,0));draw=ImageDraw.Draw(image);draw.polygon([(3,4),(18,4),(18,1),(29,11),(18,21),(18,15),(3,15)],fill=(240,40,20,255));draw.rectangle((3,22,10,29),fill=(30,220,50,255));draw.rectangle((14,25,20,29),fill=(30,70,240,255));image.save(a.output/'logo.png')
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_FILL_SYMMETRY_DEMO=str(a.output))
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=400)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-6000:])
d=json.loads((a.output/'symmetry.json').read_text());c={s['stage']:s for s in d['captures']};assert len(c)==17,list(c)
if not a.check_only:assert d['pid']==process.pid
def texture(stage):return c[stage]['textures'][0]
def hash_(stage):return texture(stage)['color_hash']
def fill(stage):return next(l for l in c[stage]['document']['texture_sets'][0]['layers']if l['kind']=='fill')
def projection(stage):return fill(stage)['fill_projection']
for left,right in [('base','mirror'),('mirror','flip-u'),('mirror','flip-v'),('mirror','radial'),('display','uv'),('mask-on','mask-off')]:assert hash_(left)!=hash_(right),(left,right)
for left,right in [('radial','display'),('display','returned'),('uv','undo'),('returned','redo'),('saved','opened'),('mask-on','mask-undo'),('mask-saved','mask-opened')]:assert hash_(left)==hash_(right),(left,right)
assert c['base']['properties_enable_visible'] and c['base']['toolbar_enabled'] and not c['base']['toolbar_checked']
assert c['mirror']['toolbar_checked'] and c['mirror']['properties_enable_visible']
assert not c['uv']['properties_enable_visible'] and not c['uv']['toolbar_enabled'] and projection('uv')['symmetry']['enabled']
assert c['returned']['properties_enable_visible'] and c['returned']['toolbar_enabled'] and c['returned']['toolbar_checked']
assert c['returned']['undo']==c['uv']['undo']+1 and c['mirror']['undo']==c['base']['undo']+1
for left,right in [('saved','opened'),('mask-on','mask-undo'),('mask-saved','mask-opened')]:assert c[left]['document']==c[right]['document'],(left,right)
# Both projected copies sample identical source data without an input flip.
samples={(s['x'],s['y']):s['rgba']for s in texture('mirror')['samples']};pairs=0
for (x,y),rgba in samples.items():
 if (127-x,y)in samples:
  assert max(abs(a-b)for a,b in zip(rgba,samples[127-x,y]))<1e-5,((x,y),rgba,samples[127-x,y]);pairs+=1
assert pairs>=20
for stage,state in c.items():
 assert state['brush_symmetry']==c['base']['brush_symmetry'],stage
 assert texture(stage)['cpu_reference_error']<1e-4,(stage,texture(stage)['cpu_reference_error'])
 assert not state['shader_failures'],stage
 image=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');assert not any(r>235 and g<20 and b>235 for r,g,b in image.getdata()),stage
assert not(a.output/'logo.png').exists() and 'BTS_SHUTDOWN clean' in (a.output/'run.log').read_text()
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','stages':list(c),'mirror_sample_pairs':pairs,'independent_u_v_flip':True,'radial_and_display_only_controls':True,'unsupported_projection_retains_options':True,'brush_symmetry_unchanged':True,'one_undo_per_toggle':True,'mask_fill_spatial_symmetry_undo_open':True,'packed_texture_open_without_original':True,'max_cpu_reference_error':max(texture(stage)['cpu_reference_error']for stage in c),'magenta_pixels':0}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
