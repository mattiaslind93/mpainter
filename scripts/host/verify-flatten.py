"""Native group→bitmap fill, separate mask, retained source, Undo and packed Open."""
from parity_test_environment import private_display
import argparse,copy,json,math,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--rectangular',action='store_true');p.add_argument('--check-only',action='store_true');p.add_argument('--gpu',action='store_true');p.add_argument('--full-resolution',action='store_true');p.add_argument('--legacy-normal',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_FLATTEN_DEMO='1',BTS_SKEW_DEMO='1',BTS_FLATTEN_VERIFY_OUTPUT='1')
if a.rectangular:env['BTS_FLATTEN_RECT']='1'
if a.gpu:env.update(BTS_VERIFY_GPU_STACK='1',BTS_RESOLUTION_LOG='1')
resolution=4096 if a.full_resolution else 512
height=1024 if a.rectangular else resolution
if a.full_resolution:env.update(BTS_FLATTEN_LARGE='1',BTS_FLATTEN_UDIM='1')
if a.legacy_normal:env['BTS_FLATTEN_LEGACY_NORMAL']='1'
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/('fixtures/two_tile_udim.obj' if a.full_resolution else 'fixtures/single_tile.obj')),'--gui-fix-resolution',str(resolution),'--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=1200 if a.full_resolution else 400)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-3500:])
proof=json.loads((a.output/'flatten.json').read_text());assert proof['flatten_ping_ms']<100,proof['flatten_ping_ms'];caps={s['stage']:s for s in proof['captures']};assert caps['empty-before']['document']==caps['empty-after']['document']
def selected(stage):
 sets=caps[stage]['document']['texture_sets'];return next(s for s in sets if s['selected_layer'] in [l['id']for l in s['layers']])
before=selected('before');after=selected('flattened');group=before['selected_layer'];source=next(l for l in after['layers']if l['id']==group);original=next(l for l in before['layers']if l['id']==group);expected=original.copy();expected['visible']=False;assert source==expected
assert len(after['layers'])==len(before['layers'])+1
flat=next(l for l in after['layers']if l['id']==after['selected_layer']);assert flat['kind']=='fill'and flat['mask']and len(flat['fill_projection']['texture']['tiles'])==len(before['tiles'])
assert caps['before']['document']==caps['after-exports']['document']
active_channels=sum(before['channels'])-int(before['channels'][3])+int(before['displacement']['channel']) # Internal Normal stays packed, never published/exported.
assert caps['flattened']['flattened_assets']==active_channels and caps['opened']['flattened_assets']==caps['flattened']['flattened_assets']
assert len(proof['exports'])==2 and len(proof['exports'][0]['files'])==2*len(before['tiles'])*active_channels and len(proof['exports'][1]['files'])==len(before['tiles'])
def stem(path):return re.sub(r'\.\d{4}$','',path.stem)
for filename in proof['exports'][0]['files']:
 path=a.output/'maps'/filename;assert path.is_file()
 if path.suffix=='.exr':assert path.read_bytes()[:4]==bytes.fromhex('762f3101') and stem(path).endswith(('_color','_bump','_displacement'))
 else:
  image=Image.open(path);assert image.size==(resolution,height)and path.read_bytes()[24]==8 and stem(path).endswith(('_color','_metallic','_roughness'))
assert all('_normal' not in Path(name).stem for name in proof['exports'][0]['files'])
# The internal normal texture is retained in the flattened layer and packed file.
assert (flat['fill_projection']['channel_textures'][2] is not None)==before['channels'][3]
for name in proof['exports'][0]['files']:
 path=a.output/'maps'/name
 if path.suffix=='.png':assert path.read_bytes()[25]==(6 if stem(path).endswith('_color') else 0),name
 else:
  import struct
  with path.open('rb') as stream:data=stream.read(65536)
  cursor=8;channels=[]
  def text():
   global cursor
   end=data.index(b'\0',cursor);value=data[cursor:end];cursor=end+1;return value
  while data[cursor]:
   attribute=text();kind=text();length=struct.unpack_from('<I',data,cursor)[0];cursor+=4;end=cursor+length
   if attribute==b'channels':
    while data[cursor]:channels.append(text());cursor+=16
    break
   cursor=end
  assert len(channels)==(4 if stem(path).endswith('_color') else 1),(name,channels)
for filename in proof['exports'][1]['files']:
 mask=a.output/'mask'/filename;assert mask.read_bytes()[24]==16 and Image.open(mask).size==(resolution,height)
assert caps['undo']['document']==caps['before']['document'];assert caps['redo']['document']==caps['flattened']['document'];# edit_state contains transient COW/page identities; compare metadata separately
# and independently require exact FP32 GPU captures of the SAME original page
# coordinates, including zero pages compacted away by persistence.
def metadata(document):
 result=copy.deepcopy(document)
 for texture_set in result['texture_sets']:
  for layer in texture_set['layers']:
   layer.pop('paint_hashes',None);layer.pop('paint_pages',None)
   for tile in layer.get('paint_tiles',[]):tile.pop('hashes',None);tile.pop('pages',None)
   if layer.get('strokes'):layer['strokes']={'present':True}
   displacement=layer['displacement']
   for tile in displacement.get('tiles',[]):tile.pop('hash',None);tile.pop('pages',None)
   if displacement.get('strokes'):displacement['strokes']={'present':True}
   if layer.get('mask'):
    for tile in layer['mask'].get('tiles',[]):tile.pop('hash',None);tile.pop('pages',None)
    if layer['mask'].get('strokes'):layer['mask']['strokes']={'present':True}
 return result
assert metadata(caps['saved']['document'])==metadata(caps['opened']['document'])
assert caps['saved']['gpu_source_pages'] and caps['saved']['gpu_source_pages']==caps['opened']['gpu_source_pages'], 'FP32 GPU source pages changed during Save/Open'
probes={}
for stage in ['before','flattened','opened']:
 probes[stage]={(p['channel'],p['tile'],p['x'],p['y']):p['value'] for p in caps[stage]['flatten_material_probes']}
assert probes['before'] and probes['before'].keys()==probes['flattened'].keys()==probes['opened'].keys()
if a.full_resolution:
 displacement=[v for (c,tile,x,y),v in probes['before'].items() if c==14];assert min(displacement)<0<max(displacement),displacement
material_error=max(abs(probes['before'][k]-probes[stage][k]) for stage in ['flattened','opened'] for k in probes['before'])
assert material_error<.001,material_error
output_probes=[p for batch in proof['exports'] for p in batch['probes']];assert output_probes
for sample in output_probes:
 if sample['layer']==group and sample['channel'] in [4,14]:
  k=sample['channel'],sample['tile'],sample['x'],sample['y'];assert abs(sample['actual']-probes['before'][k])<.001,sample
assert caps['flattened']['undo_count']==caps['before']['undo_count']+1
for name,s in caps.items():
 assert not s['shader_failures']and s['decimal']=='.'
 if name not in ['empty-before','empty-after']:assert s['source_obj_hash']==caps['before']['source_obj_hash']
if not a.full_resolution:assert caps['flattened']['composite_checked']and not caps['flattened']['composite_differences']
errors={}
for stage in ['flattened','undo','redo','opened']:
 for key in ['scene_center','scene_distance','scene_pitch','scene_yaw','scene_vertical_extent','uv_center','uv_vertical_extent']:
  original_value=caps['before']['view'][key];value=caps[stage]['view'][key];assert all(math.isclose(x,y,abs_tol=1e-6) for x,y in zip(original_value,value)) if isinstance(original_value,list) else math.isclose(original_value,value,abs_tol=1e-6),(stage,key,original_value,value)
reference=Image.open(a.output/'before-viewport.png').convert('RGB');assert max(ImageStat.Stat(reference).stddev)>8,'Blank private-FBO reference'
for name in ['flattened','undo','redo','opened']:
 candidate=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert max(ImageStat.Stat(candidate).stddev)>8,('Blank private-FBO capture',name);delta=ImageChops.difference(reference,candidate);mean=sum(ImageStat.Stat(delta).mean)/765;errors[name]=mean;assert mean<.003,(name,mean)
log=(a.output/'run.log').read_text();assert 'BTS_FLATTEN_MS'in log and f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(rectangular=a.rectangular,pid=proof['pid'],locale='sv_SE.UTF-8',retained_disabled_group=True,embedded_fill=True,separate_mask=True,batch_export_maps=2*len(before['tiles'])*active_channels,mask_png16=True,persisted_flattened_category=True,internal_normal_retained_hidden=True,legacy_normal_source_present=before['channels'][3],normal_export_disabled=True,scalar_height_headers=True,exports_do_not_edit_document=True,empty_atomic_rejection=True,one_undo=True,async_matches_sync=not a.full_resolution,gpu_material_preserved=True,packed_open_exact=True,gpu_source_pages=len(caps['saved']['gpu_source_pages']),material_error=material_error,output_probes=len(output_probes),full_resolution=a.full_resolution,view_error=errors,flatten_ping_ms=proof['flatten_ping_ms'],clean_shutdown=True)
if a.gpu:
 match=re.search(r'BTS_GPU_FLATTEN_VERIFIED cases=(\d+) max_error=([^\s]+)',log);assert match,'GPU flatten oracle missing'
 assert 'BTS_GPU_FLATTEN chunks='in log and 'cpu_composites=0'in log
 assert 'BTS_GPU_DEFERRED_THUMBNAIL_VERIFIED tiles=2 cases=5' in log
 assert 'BTS_GPU_FLATTEN_EXPORT packets=' in log and 'cpu_stack_composites=0' in log
 assert not re.search(r'BTS_GPU_LAYER_CAPTURE_FAILED|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK',log)
 result.update(deferred_thumbnail_cases=5,gpu_cases=int(match[1]),gpu_max_error=float(match[2]),gpu_compositing=True,async_chunk_readback=True,cpu_composites=0)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
