"""U: real GUI per-output encoding choices, headers and packed project roundtrip."""
from parity_test_environment import private_display
import argparse,json,os,struct,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'DepthMesh.obj';source=(root/'fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib DepthMesh.mtl');lines=[];index=0
for line in source.splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 lines.append(line)
mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'fixtures/single_tile.mtl').read_text();(a.output/'DepthMesh.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_DERIVED_EXPORT_DEMO='1',BTS_SKEW_DEMO='1',BTS_EXPORT_DEPTH_DEMO='1')
if not a.check_only:
 cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
 with (a.output/'run.log').open('w') as log:
  process=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=400)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
def depth(path):
 data=path.read_bytes()
 if path.suffix=='.png':assert data[:8]==bytes.fromhex('89504e470d0a1a0a');return ('png',data[24])
 if path.suffix=='.jpg':
  assert data[:2]==b'\xff\xd8';pos=2
  while pos<len(data):
   assert data[pos]==255;marker=data[pos+1];length=int.from_bytes(data[pos+2:pos+4],'big')
   if marker in (192,193,194):return ('jpeg',data[pos+4])
   pos+=2+length
  raise AssertionError('JPEG sample precision missing')
 assert path.suffix=='.exr' and data[:4]==bytes.fromhex('762f3101');pos=8
 while data[pos]:
  end=data.index(0,pos);name=data[pos:end];pos=end+1;end=data.index(0,pos);kind=data[pos:end];pos=end+1;length=struct.unpack_from('<I',data,pos)[0];pos+=4;value=data[pos:pos+length];pos+=length
  if name==b'channels':
   assert kind==b'chlist';offset=0;types=[]
   while value[offset]:
    offset=value.index(0,offset)+1;types.append(struct.unpack_from('<i',value,offset)[0]);offset+=16
   assert len(set(types))==1,types
   return ('exr',16 if types[0]==1 else 32 if types[0]==2 else 0)
 raise AssertionError('EXR channels missing')
d=json.loads((a.output/'derived-export.json').read_text());captures={s['stage']:s for s in d['captures']};base=captures['before']
if not a.check_only:assert d['pid']==process.pid
assert captures['saved']['document']==captures['opened']['document']
config=captures['saved']['document']['export_configuration'];assert config['version']==8
converted=next(p for p in config['presets'] if p['name'].startswith('PBR Specular'))
assert [m['format'] for m in converted['outputs']]==[0,1,2,3,None]
standard=next(p for p in config['presets'] if p['id']==config['preset']);assert standard['formats']==[0,4,1,2,1,3,1,None,3]
packed=next(p for p in config['presets'] if p['name']=='New packed preset')['packed'];assert packed['format']==2 and packed['sources'][0]==7
expected={'converted':{'diffuse':('png',8),'specular':('png',16),'glossiness':('exr',16),'bump':('exr',32),'emissive':('png',16)},'direct':{'color':('png',8),'metallic':('jpeg',8),'roughness':('png',16),'bump':('png',16),'ambient_occlusion':('exr',32),'mixed_ao':('png',16)}}
proof=[]
for folder,channels in expected.items():
 files=[f for f in (a.output/folder).iterdir() if f.suffix in ('.png','.exr','.jpg')];assert len(files)==len(channels),(folder,files)
 for suffix,encoding in channels.items():
  matches=[f for f in files if f.stem.endswith('SphereSurface_'+suffix)];assert len(matches)==1,(folder,suffix);actual=depth(matches[0]);assert actual==encoding,(matches[0],actual,encoding);proof.append({'file':str(matches[0]),'codec':actual[0],'bits':actual[1]})
for name,s in captures.items():
 before=base['document'].copy();current=s['document'].copy();before.pop('export_configuration');current.pop('export_configuration');assert before==current,name
 assert s['decimal']=='.' and not s['shader_failures'] and s['source_obj_hash']==base['source_obj_hash']
for name in ['after-converted','after-direct']:
 before=Image.open(a.output/'before-viewport.png').convert('RGB');after=Image.open(a.output/(name+'-viewport.png')).convert('RGB');difference=sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/765;assert difference<.001,(name,difference)
(a.output/'verified.json').write_text(json.dumps({'pid':d['pid'],'files':proof,'schema':6,'all_encoding_scopes_saved_opened':True,'source_and_viewport_unchanged':True},indent=2)+'\n');print('U GUI export precision, file headers and packed Save/Open passed',d['pid'])
