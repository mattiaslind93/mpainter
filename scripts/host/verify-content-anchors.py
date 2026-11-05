"""Private native content-anchor creation, channel extraction, navigation and packed Open."""
import argparse,json,os,re,subprocess,struct,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');p.add_argument('--fill',action='store_true');p.add_argument('--generator',action='store_true');a=p.parse_args();assert not(a.fill and a.generator); out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','Run in a private offscreen environment'
root=Path(__file__).resolve().parents[2];base=root.parent
for directory in ('config','data','cache','tmp','recovery','logs','resources'):(out/directory).mkdir(exist_ok=True)
if not a.existing:
 env=dict(os.environ,BTS_CONTENT_ANCHORS='1',BTS_HEADLESS_WORKSPACE='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
 if a.generator:env['BTS_GENERATOR_ANCHORS']='1'
 if a.fill:
  env['BTS_FILL_ANCHORS']='1'
  def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
  raw=b''.join(b'\0'+bytes([128,64,32])*2 for _ in range(2))
  (out/'anchor-color.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',2,2,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
 env.update({name:str(out/sub) for name,sub in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('BTS_RESOURCE_CACHE','resources')]})
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(base/'build/baseline-demo/host-fixture.blend'),'--environment',str(base/'build/environment-cache/TT_environmentLights_v001.hdr'),'--new-mesh',str(root/'native/tests/fixtures/two_materials.obj'),'--new-demo-dir',str(out),'--anchor-reference-demo',str(base/'build/mesh-maps-native/mesh-maps.blend'),'--recovery-dir',str(out/'recovery')]
 with (out/'run.log').open('w') as log:process=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=240)
 assert process.returncode==0,process.returncode
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert not re.search(r'BTS_GPU_AUTHORING_CPU_READ|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_GPU_STACK_ERROR|BTS_FINALIZE_FALLBACK|BTS_FEATURE_DEMO_FAILED',log)
proof=json.loads((out/'paint.json').read_text());captures=proof['captures'];assert len(captures)==(51 if a.fill or a.generator else 22),len(captures)
def doc(i):return captures[i]['document']['texture_sets'][0]
def data(i):return captures[i]['content_anchor']
expected={7:.2,8:.4,9:.4,10:.4,11:0,12:.35,13:0,14:0,15:1,17:1,18:1,19:1,20:.6,21:0};maximum=0
for i,value in expected.items():
 d=data(i);assert len(d['samples'])==3 and all(x is not None for x in d['samples']),(i,d)
 error=max(abs(x-value) for x in d['samples']+[d['cpu']]);maximum=max(maximum,error);assert abs(d['cpu']-value)<1e-6 and error<.001,(i,d,value)
 assert not d['warning'] and d['extract_visible'],(i,d)
for i in (2,3,16):assert data(i)['source_properties'] and not data(i)['selected_mask'],(i,data(i))
assert data(16)['references']==1 and data(17)['selected_mask']
assert doc(11)==doc(13) and doc(18)==doc(19)
for i in range(1,22):assert captures[i]['document']['texture_sets'][1]==captures[1]['document']['texture_sets'][1],i
point=next(e for e in doc(3)['layers'][0]['effects'] if e['type']=='anchor');assert point['name']=='Raw content'
assert (out/'content-anchors.blend').is_file()
result={'stages':22,'native_gpu_cpu_max_error':maximum,'real_anchor_creation_and_naming':True,'mask_consumes_content_color_bump_displacement_user':True,'raw_hidden_zero_opacity_masked_source':True,'coverage_option':True,'bidirectional_navigation':True,'source_edit_undo_and_packed_open':True,'other_texture_set_unchanged':True,'scope':'Content prefixes to Mask Fill. General Fill-layer and generator input slots remain separate work.'}
if a.fill:
 def fill(i):return captures[i]['fill_anchor']
 def samples(i,c):return next(x for x in fill(i)['channels'] if x['channel']==c)
 errors=[]
 for i in range(22,51):
  d=fill(i);assert not d['selected_mask'] and d['normal_input_absent'] and not d['warning'],(i,d)
  if i>=25 and i not in (35,36,37):assert d['options_visible'],(i,d)
  for channel in d['channels']:
   if channel['channel']==6 and i<27 and any(x is None for x in channel['gpu']):continue # Uniform, uninspected user output has no published GPU override yet.
   assert all(x is not None for x in channel['gpu']),(i,channel)
   delta=max(abs(x-y) for x,y in zip(channel['gpu'],channel['cpu']));errors.append(delta);assert delta<.001*max(1,max(abs(x) for x in channel['cpu'])),(i,channel)
  assert captures[i]['document']['texture_sets'][1]==captures[1]['document']['texture_sets'][1],i
 expected_bump={**{i:0 for i in range(22,25)},**{i:-.25 for i in range(25,51)},28:.4,29:1,31:.35,32:.35,37:0,41:.6,42:.6}
 for i,value in expected_bump.items():assert abs(samples(i,4)['cpu'][0]-value)<1e-6,(i,value,samples(i,4))
 point=data(3)['point'].strip('{}')
 for i,count in [(22,0),(23,1),(24,2),(25,3),(26,4),(27,5),(37,4),(38,5),(45,4),(46,5),(47,4),(49,4),(50,5)]:
  refs=[ref for ref in fill(i)['references'] if ref['source']];assert len(refs)==count and all(ref['source']==point for ref in refs),(i,refs)
 assert fill(35)['source_properties'] and fill(35)['source_references']==5
 assert not fill(36)['source_properties'] and fill(36)['selected_layer']==data(7)['consumer']
 for left,right in [(30,34),(39,40),(39,44),(48,49)]:assert doc(left)==doc(right),(left,right)
 assert not (out/'anchor-color.png').exists() and fill(49)['image_resolved']
 assert fill(45)['image'] and not fill(46)['image'] and fill(47)['image'] and not fill(50)['image']
 for i in [23,27,34,40,44,46,50]:assert max(abs(x-y) for x,y in zip(samples(i,0)['cpu'],[.2,.4,.6]))<1e-6,i
 assert samples(45,0)['cpu']!=samples(46,0)['cpu'] and samples(45,0)['cpu']==samples(47,0)['cpu']==samples(49,0)['cpu']
 result['fill_channels']={'stages':29,'channels':[0,2,4,6,14],'gpu_cpu_max_error':max(errors),'real_header_source_menus':True,'extraction':True,'disabled_hidden_source_edits':True,'source_edit_undo':True,'bidirectional_navigation':True,'uniform_clear_undo':True,'packed_open':True,'image_replaces_anchor_undo_redo':True,'packed_image_without_original':True,'normal_input_hidden':True}
 result['scope']='Mask Fill and per-channel Fill consume content/mask anchors. Generator input slots remain pending.'
 (out/'fill-anchors-verified.json').write_text(json.dumps(result,indent=2)+'\n')
if a.generator:
 def generator(i):return captures[i]['generator_anchor']
 expected={22:.15,23:.15,24:.2,25:.4,26:0,27:.7,28:0,29:.4,30:.15,31:.54,32:.46,33:.54,34:.54,35:.54,36:.54,37:.54,38:.15,39:.54,40:.25,41:.25,42:.2,43:0,44:.8,45:0,46:.2,47:.2,48:.2,49:.2,50:.2}
 errors=[]
 for i,value in expected.items():
  d=generator(i);assert d['gpu'] is not None,(i,d);error=abs(d['gpu']-d['cpu']);errors.append(error);assert error<.001 and abs(d['cpu']-value)<1e-6,(i,d,value)
  assert d['normal_input_absent'] and (d['warning']==(i==38)),(i,d)
  assert captures[i]['document']['texture_sets'][1]==captures[1]['document']['texture_sets'][1],i
 assert generator(23)['bake'] and generator(30)['bake'] and not generator(24)['bake'] and not generator(31)['bake']
 assert generator(34)['source_properties'] and generator(34)['references']==2 and generator(49)['source_properties'] and generator(49)['references']==1
 assert not generator(35)['source_properties'] and not generator(50)['source_properties']
 for left,right in [(36,37),(36,39),(47,48)]:
  assert doc(left)==doc(right),(left,right)
 result['generator_inputs']={'stages':29,'gpu_cpu_max_error':max(errors),'mask_and_content':True,'dirt_two_independent_inputs':True,'map_fallback_and_missing_map_cta':True,'broken_source_warning':True,'live_source_edit_undo':True,'bidirectional_navigation':True,'packed_open':True,'normal_input_hidden':True}
 result['scope']='Mask Fill, per-channel Fill and mesh-generator input overrides consume live content/mask anchors.'
 (out/'generator-anchors-verified.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'content-anchors-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
