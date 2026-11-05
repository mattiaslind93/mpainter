"""Private native generator controls, real asynchronous map replacement and GPU parity."""
import argparse,json,math,os,re,struct,subprocess,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');p.add_argument('--content',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];base=root.parent
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','Use an isolated offscreen invocation'
fixtures=out/'generator-inputs';fixtures.mkdir(exist_ok=True)
for name in ('config','data','cache','tmp','recovery','logs','resources'):(out/name).mkdir(exist_ok=True)
private_env={'XDG_CONFIG_HOME':str(out/'config'),'XDG_DATA_HOME':str(out/'data'),'XDG_CACHE_HOME':str(out/'cache'),'TMPDIR':str(out/'tmp'),'BTS_RECOVERY_DIR':str(out/'recovery'),'BTS_SESSION_LOG_DIR':str(out/'logs'),'BTS_RESOURCE_CACHE':str(out/'resources'),'BTS_HEADLESS_WORKSPACE':'1'}
def png(path,w,h,pixel):
 def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
 raw=b''.join(b'\0'+bytes(c for x in range(w) for c in pixel(x,y)) for y in range(h))
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
if not a.existing:
 for family in ('position','curvature','ao'):
  for flipped in (False,True):
   def pixel(x,y):
    if family=='position':
     value=round(255*x/127);return (51,204,255-value if flipped else value)
    low,high=(32,224) if family=='curvature' else (51,204)
    value=(high if x<64 else low) if flipped else (low if x<64 else high);return (value,)*3
   png(fixtures/f"{family}-{'flipped' if flipped else 'forward'}.png",128,128,pixel)
 png(fixtures/'thickness-forward.png',128,128,lambda x,y:(64 if x<64 else 192,)*3)
 png(fixtures/'thickness-flipped.png',128,128,lambda x,y:(192 if x<64 else 64,)*3)
 png(fixtures/'normal-up.png',128,128,lambda x,y:(128,128,255))
 png(fixtures/'normal-side.png',128,128,lambda x,y:(255,128,128))
 png(fixtures/'invalid.png',31,64,lambda x,y:(0,0,0))
 cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(base/'build/baseline-demo/host-fixture.blend'),'--environment',str(base/'build/environment-cache/TT_environmentLights_v001.hdr'),'--new-mesh',str(root/'native/tests/fixtures/two_materials.obj'),'--new-demo-dir',str(out),'--mesh-generator-demo',str(base/'build/mesh-maps-native/mesh-maps.blend'),'--generator-fixtures',str(fixtures),'--recovery-dir',str(out/'recovery')]
 with (out/'run.log').open('w') as log:
  proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=420,env={**os.environ,**private_env,'BTS_MESH_GENERATORS':'1',**({'BTS_CONTENT_GENERATORS':'1'} if a.content else {}),'LANG':'sv_SE.UTF-8','LC_ALL':'sv_SE.UTF-8'})
 assert proc.returncode==0,proc.returncode
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert not re.search(r'BTS_GPU_AUTHORING_CPU_READ|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_GPU_STACK_ERROR|BTS_FINALIZE_FALLBACK|BTS_FEATURE_DEMO_FAILED',log)
captures={c['stage']:c for c in json.loads((out/'paint.json').read_text())['captures']};assert len(captures)==(92 if a.content else 62),len(captures)
def doc(stage):return captures[stage]['document']['texture_sets'][0]
def data(stage):return captures[stage]['mesh_generator']
def effect(stage):return next(e for e in doc(stage)['layers'][-1]['mask']['effects'] if e['type']=='generator')
def equal(left,right):assert doc(left)==doc(right),(left,right)
for left,right in [('position','undo-input'),('position','restored-input'),('curvature','reopened'),('curvature','undo-invert'),('ao-input','ao-undo'),('dirt','dirt-undo'),('dirt','dirt-input-undo'),('light-x','light-input-undo'),('extended-saved','extended-open'),('extended-open','light-edit-undo'),('light-on','extended-invalid'),('light-on','extended-cancelled'),('light-on','light-restored')]:equal(left,right)
errors={};oracle={};other=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1]
roles={1:5,2:4,3:3,4:6,5:3,6:1}
def sample(mapping,x):
 name=mapping['name']
 if 'position-' in name:
  value=round(255*x/127);return [51/255,204/255,(255-value if 'flipped' in name else value)/255]
 if 'curvature-' in name:lo,hi=32,224
 elif 'ao-' in name:lo,hi=51,204
 elif 'thickness-' in name:lo,hi=64,192
 elif 'normal-up' in name:return [128/255,128/255,1]
 elif 'normal-side' in name:return [1,128/255,128/255]
 else:return None
 return [((hi if x<64 else lo) if 'flipped' in name else (lo if x<64 else hi))/255]*3
for stage,c in list(captures.items())[1:62]:
 d=data(stage);assert c['document']['texture_sets'][1]==other and doc(stage)['layers'][:-1]==lower,stage
 assert d['sphere']==data('setup')['sphere'],stage
 if stage=='setup':continue
 e=effect(stage);g=e['generator'];assert d['properties'] and not d['paint_enabled'],stage
 assert d['axis_visible']==(g==1) and d['light_visible']==(g==6) and d['dirt_visible']==(g==5),stage
 gpu=d['gpu_samples'];cpu=d['samples'];assert len(gpu)==len(cpu)==16 and all(v is not None for v in gpu),(stage,gpu)
 errors[stage]=max(abs(x-y) for x,y in zip(gpu,cpu));assert errors[stage]<.001,(stage,errors[stage],gpu,cpu)
 expected=[]
 for x in range(4,128,8):
  raw=None;source=doc(stage)['mesh_maps'][roles[g]] if g else None
  if not e['enabled'] or not g or source is None or (g==5 and doc(stage)['mesh_maps'][4] is None):expected.append(.75);continue
  rgb=sample(source,x)
  if rgb is None:break
  if g==1:raw=rgb[e['axis']]
  elif g==5:
   curve=sample(doc(stage)['mesh_maps'][4],x)
   if curve is None:break
   raw=(1-rgb[0])*(1-e['dirt_curvature']*curve[0])
  elif g==6:
   normal=[2*v-1 for v in rgb];direction=e['light_direction'];length=math.sqrt(sum(v*v for v in normal)*sum(v*v for v in direction));raw=max(0,sum(n*l for n,l in zip(normal,direction))/length) if length>1e-12 else 0
  else:raw=rgb[0]
  value=max(0,min(1,(raw-.5)*e['contrast']+.5+e['bias']));value=1-value if e['invert'] else value
  if e.get('curve')==1:value=value*value*(3-2*value)
  elif e.get('curve')==2:value=value*value
  expected.append(.75*(1-e['opacity'])+value*e['opacity'])
 if len(expected)==16:
  oracle[stage]=max(abs(x-y) for x,y in zip(cpu,expected));assert oracle[stage]<2e-7,(stage,oracle[stage])
assert all(v==0 for v in data('light-zero')['samples'])
assert data('light-input')['samples']!=data('light-x')['samples']
assert data('light-missing')['bake_visible'] and data('bake-requested')['bake_opened']
assert not data('light-restored')['bake_visible']
result={'offscreen':True,'stages':len(captures),'gpu_cpu_max_error':max(errors.values()),'closed_form_max_error':max(oracle.values()),'closed_form_stages':len(oracle),'resources':['World Height','Curvature','Ambient Occlusion','Thickness','Dirt','World Normal Light'],'async_file_replacement':True,'dirt_two_inputs':True,'zero_direction_finite':True,'missing_input_bake_button':True,'undo_and_packed_open':True,'other_texture_set_unchanged':True,'scope':'Native actions use explicit CPU diagnostic queries between captures; separate AA gate measures interaction latency.'}
if a.content:
 content={name:c['content_generator'] for name,c in list(captures.items())[62:]}
 assert len(content)==30
 assert all(d['gpu_error']<.01 for d in content.values()),{k:d['gpu_error'] for k,d in content.items()}
 for name,d in content.items():
  assert 3 not in d['channels'],name
  if name not in ('content-setup','content-empty','content-fill','content-mask','content-mask-return'):assert d['panel_visible'] and d['channel_visible'],name
 assert not content['content-empty']['channel_visible']
 assert not content['content-mask']['channel_visible'] and content['content-mask']['mask']
 assert content['content-return']['channel_visible'] and not content['content-return']['mask']
 assert not content['content-mask-return']['channel_visible'] and content['content-mask-return']['mask']
 for left,right in [('content-thickness','content-input-undo'),('content-thickness','content-opacity-undo'),('content-thickness','content-on'),('content-fill-generator','content-blend-undo'),('content-saved','content-open'),('content-open','content-undo-open'),('content-coexist-save','content-coexist-open')]:equal(left,right)
 def image(stage,channel):return [p[2:] for p in content[stage]['samples'] if p[0]==channel]
 assert image('content-height',0)!=image('content-empty',0)
 assert image('content-roughness',0)==image('content-empty',0),'Moving generator to Roughness changed Color'
 assert image('content-roughness',2)!=image('content-empty',2)
 assert image('content-input',4)!=image('content-thickness',4)
 assert image('content-off',4)!=image('content-on',4)
 assert image('content-blend',0)!=image('content-fill-generator',0)
 assert image('content-edited-open',0)!=image('content-open',0)
 result['content']={'stages':len(content),'maximum_gpu_error':max(d['gpu_error'] for d in content.values()),'empty_paint_produces_coverage':True,'channel_local_color_roughness_bump_displacement':True,'fill_resource_and_blending':True,'source_replacement_undo':True,'packed_open':True,'mask_and_content_coexist_and_navigate':True,'hidden_normal_unchanged':True}
(out/'generators-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
