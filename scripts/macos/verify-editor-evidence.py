#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently verify offscreen editor state and exported PNG/EXR pixels."""
import argparse
import json
import math
import os
from pathlib import Path
import runpy
from PIL import Image, ImageChops, ImageStat

p=argparse.ArgumentParser(description=__doc__)
for name in ['paint','layers','bake','export','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
priority=os.getpriority(os.PRIO_PROCESS,0)
if priority<15:os.nice(15-priority)
report={}
def read(directory,name):
 assert json.loads((directory/'result.json').read_text())['passed'],directory
 return json.loads((directory/name).read_text())
def captures(d):return {c['stage']:c for c in d['captures']}
c=captures(read(a.paint,'paint.json'))
def sphere(stage):return next(t for t in c[stage]['document']['texture_sets'] if t['name']=='SphereSurface')
def layer(stage):return sphere(stage)['layers'][-1]
assert layer('large-3d')['strokes']['operations']==2 and layer('large-3d')['strokes']['spatial']==2
assert layer('uv-stroke')['strokes']['operations']==3 and layer('uv-stroke')['strokes']['spatial']==2
assert not layer('rough-disabled')['channels'][2]
assert layer('rough-disabled')['strokes']['operations']==layer('large-3d')['strokes']['operations']
assert layer('metal-stroke')['paint_hashes'][2]==layer('rough-disabled')['paint_hashes'][2]
assert layer('metal-stroke')['paint_hashes'][1]!=layer('large-3d')['paint_hashes'][1]
for before,after in [('undo-stroke','rough-disabled'),('redo-stroke','metal-stroke'),('cancelled','uv-stroke'),('layer-shown','uv-stroke')]:
 assert c[before]['document']==c[after]['document'],(before,after)
def authoring_state(value):
 # CPU diagnostic caches are regenerated from stored GPU channel precision.
 # Compare every other metadata field and separately compare rendered pixels.
 if isinstance(value,dict):return {k:authoring_state(v) for k,v in value.items() if k not in ['paint_hashes','hashes','flat_hash']}
 if isinstance(value,list):return [authoring_state(v) for v in value]
 return value
assert authoring_state(c['saved']['document'])==authoring_state(c['reopened']['document'])
images=[Image.open(a.paint/(stage+'-viewport.png')).convert('RGB') for stage in ['saved','reopened']]
assert images[0].size==images[1].size
pixel_mean=ImageStat.Stat(ImageChops.difference(*images)).mean
assert max(pixel_mean)<.05,pixel_mean
assert c['saved']['color']==c['reopened']['color']
assert not c['reopened']['workspace']['undo_enabled'] and not c['reopened']['workspace']['redo_enabled']
assert layer('reopened')['id']==layer('edited-reopened')['id']
assert all(layer('reopened')['paint_hashes'][ch]!=layer('edited-reopened')['paint_hashes'][ch] for ch in [0,1,4])
cube=next(t for t in c['empty']['document']['texture_sets'] if t['name']=='CubeSurface')
assert all(next(t for t in v['document']['texture_sets'] if t['name']=='CubeSurface')==cube for v in c.values())
assert all((a.paint/n).stat().st_size>10000 for n in ['painted.blend','edited.blend'])
assert 'BTS_FAILED_SAVE_PRESERVED' in (a.paint/'editor.log').read_text()
assert 'BTS_STORED_GPU_ROUNDTRIP_EXACT' in (a.paint/'editor.log').read_text()
report['paint']={'stages':list(c),'save_open_authoring_metadata_preserved':True,'save_open_viewport_mean_absolute_error_8bit':pixel_mean,'cache_hashes_bit_exact':c['saved']['document']==c['reopened']['document'],'stored_gpu_second_save_open_bit_exact':True,'edit_after_open':True,'undo_redo_cancel_channels_and_other_set':True,'failed_save_preserved':True}
d=read(a.layers,'layer-edit.json');c=captures(d);ops=d['operations'];assert len(ops)==23
assert sum(v['pages']>0 for v in c['painted-three']['layer_items'])==3
for op in ops:
 assert op['after']['undo_count']==op['before']['undo_count']+1
 assert op['after']['hash_bytes']==op['before']['hash_bytes']
assert c['reference']['composite_checked'] and not c['reference']['composite_differences']
assert c['final']['layer_items']==c['redo']['layer_items']==c['opened']['layer_items']
assert c['undo']['layer_items']!=c['final']['layer_items']
assert c['saved']['probe']==c['opened']['probe']
assert all(not v['shader_failures'] and not v['composite_failed_jobs'] for v in c.values())
report['layers']={'resolution':d['resolution'],'operations':len(ops),'cpu_gpu_reference_equal':True,'undo_redo_save_open':True,'max_edit_ms':max(o['return_ms'] for o in ops),'max_visible_ms':max(o['visible_ms'] for o in ops)}
d=read(a.bake,'bake-fix.json');c=captures(d);dialogs={x['stage']:x for x in d['dialogs']}
assert dialogs['default']['enabled'] and dialogs['default']['project_mesh']
for stage in ['no-source','no-maps','no-tiles','no-cage','saved-explicit-source-off']:
 assert not dialogs[stage]['enabled'] and dialogs[stage]['tooltip']==dialogs[stage]['help']
assert c['preferences-opened']['bake_configuration']==c['settings-saved']['bake_configuration']
assert {v['role'] for v in c['baked']['mesh_maps']}=={3,4}
assert not c['undo']['mesh_maps']
assert c['baked']['mesh_maps']==c['redo']['mesh_maps']==c['maps-opened']['mesh_maps']
assert all(not v['shader_failures'] for v in c.values())
report['bake']={'maps':['AO','Curvature'],'resolution':c['baked']['bake_configuration']['resolution'],'rays':c['baked']['bake_configuration']['rays'],'undo_redo_save_open':True,'invalid_configuration_rejected':True}
readers=runpy.run_path(str(Path(__file__).resolve().parents[1]/'host/export-file-check.py'))
c=captures(read(a.export,'paint.json'));base=dict(c['loaded']['document']);base.pop('export_configuration',None)
assert base['texture_sets'][1]['precision'][2]==1  # Fixture roughness is L8.
files=[]
for encoding in ['png8','png16','exr16','exr32']:
 doc=dict(c[encoding]['document']);doc.pop('export_configuration',None);assert doc==base
 assert c[encoding]['view']['scene_view_matrix']==c['loaded']['view']['scene_view_matrix']
 evidence=c[encoding]['exports'][-1];assert evidence['count']=='4 textures selected'
 assert 'Export successfully finished' in evidence['console']
 assert len(evidence['files'])==4 and not any('normal' in n for n in evidence['files'])
 for name in evidence['files']:
  path=a.export/encoding/name
  data=readers['png_pixels' if encoding.startswith('png') else 'exr_pixels'](path)
  assert data['width']==data['height']==base['resolution']
  if encoding.startswith('png'):assert data['bits']==(8 if encoding=='png8' else 16)
  else:assert set(data['types'].values())=={1 if encoding=='exr16' else 2}
  samples=[data['sample'](int(u*data['width']),int(v*data['height'])) for u,v in [(.125,.25),(.5,.5),(.75,.75)]]
  assert all(math.isfinite(v) for sample in samples for v in sample)
  tolerance={'png8':1/255,'png16':1/65535,'exr16':.0006,'exr32':1e-6}[encoding]
  if '_roughness.' in name:assert abs(samples[1][0]-round(.123456*255)/255)<tolerance,(encoding,name,samples)
  if '_bump.' in name:assert abs(samples[1][0]-(0 if encoding.startswith('png') else -.125))<tolerance,(encoding,name,samples)
  files.append({'path':str(path),'bytes':path.stat().st_size,**{k:v for k,v in data.items() if k!='sample'},'samples':samples})
assert c['retained']['exports'][-1]=={'retained_format':'exr','retained_precision':'32f bits (FLOAT)','retained_count':'4 textures selected'}
report['export']={'files':files,'four_real_encodings':True,'signed_height_exr_and_clamped_png':True,'raw_roughness':True,'document_and_camera_preserved':True}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:{x:y for x,y in v.items() if x!='files'} for k,v in report.items()},indent=2))
