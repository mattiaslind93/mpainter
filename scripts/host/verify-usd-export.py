"""Painter-style five-map USD preset, native material graph, self-contained package and Open."""
from parity_test_environment import private_display
import argparse,copy,json,os,re,struct,subprocess,zipfile
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--displacement',action='store_true');p.add_argument('--udim',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'UsdPaint.obj';lines=[];index=0
for line in (root/'fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib UsdPaint.mtl').splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 lines.append(line)
if a.udim:
 counts=[sum(line.startswith(prefix) for line in lines) for prefix in ['v ','vt ','vn ']];extra=[]
 for line in lines:
  if line.startswith('mtllib '):continue
  if line.startswith('v '):values=line.split();values[1]=str(float(values[1])+4);line=' '.join(values)
  elif line.startswith('vt '):values=line.split();values[1]=str(float(values[1])+1);line=' '.join(values)
  elif line.startswith('o '):line='o Tile2_'+line[2:]
  elif line.startswith('f '):line='f '+' '.join('/'.join(str(int(value)+counts[i]) if value else '' for i,value in enumerate(vertex.split('/'))) for vertex in line.split()[1:])
  extra.append(line)
 lines+=extra
mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'fixtures/single_tile.mtl').read_text();(a.output/'UsdPaint.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_DERIVED_EXPORT_DEMO='1',BTS_USD_EXPORT_DEMO='1',BTS_SKEW_DEMO='1')
if a.displacement:env['BTS_USD_DISPLACE_DEMO']='1'
if a.udim:env['BTS_USD_UDIM_DEMO']='1'
tiles=[1001,1002] if a.udim else [None]
def inspect(path,label):
 e=dict(env,QT_QPA_PLATFORM='offscreen',BTS_USD_MESH_VERIFY_INPUT=str(path))
 with (a.output/(label+'.log')).open('w')as log:code=subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--cpu-export-test',str(a.output/label)],env=e,stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
 assert code==0,(a.output/(label+'.log')).read_text()[-3000:]
 return json.loads((a.output/label/'usd-inspected.json').read_text())
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=400)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
 inspect(a.output/'converted/UsdPaint.usdc','loose-stage');inspect(a.output/'converted/UsdPaint.usdz','package-stage')
 # Resolve the package in a fresh native process after removing every loose dependency.
 loose=[p for p in (a.output/'converted').iterdir()if p.suffix in ['.jpg','.png','.usdc']]
 for path in loose:path.rename(path.with_suffix(path.suffix+'.held'))
 try:inspect(a.output/'converted/UsdPaint.usdz','self-contained-stage')
 finally:
  for path in loose:path.with_suffix(path.suffix+'.held').rename(path)
proof=json.loads((a.output/'derived-export.json').read_text());captures={s['stage']:s for s in proof['captures']};base=captures['before'];assert base['document']['normal_format']=='DirectX'
def structure(document):
 result=copy.deepcopy(document)
 result.pop('export_configuration')
 # A live GPU brush recipe hashes its CPU diagnostic replay; after Open it
 # hashes persisted authoritative GPU pages. Compare metadata here and actual
 # exported pixels below, rather than equating these two representations.
 for texture in result['texture_sets']:
  for layer in texture['layers']:
   layer.pop('paint_hashes',None)
   for tile in layer.get('paint_tiles',[]):tile.pop('hashes',None)
   if isinstance(layer.get('strokes'),dict):layer['strokes'].pop('flat_hash',None)
 return result
for name,s in captures.items():
 assert structure(base['document'])==structure(s['document'])and s['source_obj_hash']==base['source_obj_hash'],name
 assert s['decimal']=='.'and not s['shader_failures']
 if name!='opened':
  for key in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][key]==base['view'][key],(name,key)
assert structure(captures['saved']['document'])==structure(captures['opened']['document']);assert captures['saved']['document']['export_configuration']==captures['opened']['document']['export_configuration'];config=captures['saved']['document']['export_configuration'];assert config['version']==7
preset=next(p for p in config['presets']if p['id']==config['preset']);assert preset['kind']==1 and preset['name']=='USD PBR Metal Roughness'
assert len(proof['exports'][0]['files'])==4*len(tiles)+2 and proof['exports'][0]['based_on_template']and proof['exports'][0]['format_disabled']and proof['exports'][0]['displacement_enabled']==a.displacement and proof['exports'][0]['displacement_checked']==a.displacement
converted=a.output/'converted';images=[p for p in converted.iterdir()if p.suffix in ['.jpg','.png']];assert len(images)==4*len(tiles) and sum(p.suffix=='.jpg'for p in images)==4*len(tiles) and not any('normal' in p.name for p in images)
for path in images:
 image=Image.open(path);assert image.size==(512,512)and image.format==('JPEG'if path.suffix=='.jpg'else'PNG');assert 'SphereSurface' in path.name and 'CubeSurface'not in path.name
 suffix=path.stem.split('SphereSurface_',1)[1];assert suffix==suffix.lower()and 'Height'not in path.name
roundtrip_pixels={}
for original in (a.output/'direct').glob('*.png'):
 reopened=a.output/'reopened'/original.name;assert reopened.exists(),reopened
 assert original.read_bytes()==reopened.read_bytes(),('Save/Open encoded PNG16 mismatch',original.name)
 assert Image.open(original).mode==Image.open(reopened).mode and Image.open(original).size==Image.open(reopened).size
 assert Image.open(original).tobytes()==Image.open(reopened).tobytes(),('Save/Open pixel mismatch',original.name)
 roundtrip_pixels[original.name]='exact encoded PNG16 and decoded pixels'
assert roundtrip_pixels
errors={}
for role in ['color','metallic','roughness','mixed_ao']:
 for tile in tiles:
  suffix='_'+str(tile) if tile else ''
  out=Image.open(next(converted.glob('*_'+role+suffix+'.jpg'))).convert('RGB')
  reference=Image.open(next((a.output/'direct').glob('*_'+role+suffix+'.png')))
  if reference.mode in ['I;16','I','F']:reference=reference.convert('F').point(lambda i:i/257).convert('L')
  reference=reference.convert('RGB')
  delta=ImageChops.difference(out,reference);maximum=max(hi for lo,hi in delta.getextrema());mean=sum(ImageStat.Stat(delta).mean)/3;assert maximum<=3 and mean<.8,(role,maximum,mean);errors[role+suffix]=dict(maximum=maximum,mean=mean)
for label in ['loose-stage','package-stage','self-contained-stage']:
 data=json.loads((a.output/label/'usd-inspected.json').read_text());assert data['preview_shaders']==2
 if a.displacement:assert data['points']>1324 and data['triangles']>2640
 else:assert data['points']==1324*len(tiles) and data['triangles']==2640*len(tiles) and len(data['meshes'])==2*len(tiles)
 assert set(data['materials'])=={'SphereSurface','CubeSurface'}and all(m['material']in data['materials']and m['uvs']>0 for m in data['meshes'])
 assert len(data['texture_assets'])==4 and all(not Path(p).is_absolute()for p in data['texture_assets']) and not any('normal' in p for p in data['texture_assets'])
 shaders={s['path']:s for s in data['shaders']};surface=next(s for s in shaders.values()if s['id']=='UsdPreviewSurface'and '/SphereSurface/'in s['path'])
 for channel,role in [('diffuseColor','color'),('metallic','metallic'),('roughness','roughness'),('occlusion','mixed_ao')]:
  connections=surface['inputs'][channel]['connections'];assert len(connections)==1,(label,channel)
  texture=shaders[connections[0].split('.outputs:')[0]];assert texture['id']=='UsdUVTexture';inputs=texture['inputs'];assert inputs['file']['value'].endswith('_'+role+('_<UDIM>' if a.udim else '')+'.jpg')and (a.udim or inputs['file']['resolved'])
  assert inputs['sourceColorSpace']['value']==('sRGB'if channel=='diffuseColor'else'raw'),(channel,inputs)
  if a.udim:
   for tile in tiles:
    asset=inputs['file']['value'].replace('<UDIM>',str(tile))
    if label=='loose-stage':assert (converted/asset).is_file(),asset
    else:
     with zipfile.ZipFile(converted/'UsdPaint.usdz')as archive:assert asset in archive.namelist(),asset
 assert 'normal' not in surface['inputs']
with zipfile.ZipFile(converted/'UsdPaint.usdz')as archive:
 members=archive.infolist();assert len(members)==1+4*len(tiles) and members[0].filename.endswith('.usdc')and archive.read(members[0]).startswith(b'PXR-USDC')
 with (converted/'UsdPaint.usdz').open('rb')as file:
  for member in members:
   assert member.compress_type==zipfile.ZIP_STORED;file.seek(member.header_offset);header=file.read(30);n,e=struct.unpack_from('<HH',header,26);assert(member.header_offset+30+n+e)%64==0
for name in ['after-converted','after-direct']:
 im1=Image.open(a.output/'before-viewport.png').convert('RGB');im2=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert sum(ImageStat.Stat(ImageChops.difference(im1,im2)).mean)/765<.001,name
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proof['pid'],locale='sv_SE.UTF-8',painter_usd_preset_four_maps=True,real_shader_connections=True,normal_map_count=0,codec_error_in_8bit_levels=errors,save_open_exact_pixels=roundtrip_pixels,whole_mesh_selected_texture_set_only=True,usdz_members_aligned64=len(members),udim=a.udim,displacement=a.displacement,self_contained_without_loose_files=True,saved_preset_restored=True,scene_layers_camera_undo_unchanged=True,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
