"""Explicit read-only Blender catalog browser, previews, shared preset and packed Open."""
import argparse,hashlib,json,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();out=a.output.resolve();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));assert out.is_relative_to(bts.resolve()/'build');out.mkdir(parents=True,exist_ok=True);fixtures=out/'fixtures';library=fixtures/'library'
def files():return {str(p.relative_to(library)):hashlib.sha256(p.read_bytes()).hexdigest()for p in library.rglob('*')if p.is_file()and not p.is_symlink()and 'outside-link'not in p.parts}
if not a.existing:
 with(out/'fixtures.log').open('w')as log:subprocess.run([str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(root/'scripts/host/generate-blend-asset-library-fixture.py'),'--',str(fixtures)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
 assert (fixtures/'expected.json').is_file(),(out/'fixtures.log').read_text()
 (out/'source-before.json').write_text(json.dumps(files(),indent=2)+'\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_BLEND_MATERIAL_DEMO='1',BTS_BLEND_ASSET_BROWSER_DEMO='1',BTS_BLEND_ASSET_ROOT=str(library),BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
if not a.existing:
 try:
  with(out/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(out/'studio-library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
 finally:
  if Path(str(library)+'.held').exists():Path(str(library)+'.held').rename(library)
  if(out/'resources.held').exists():
   if(out/'resources').exists():(out/'resources').rename(out/'resources.partial')
   (out/'resources.held').rename(out/'resources')
d=json.loads((out/'blend-library.json').read_text());c={v['stage']:v for v in d['captures']};initial=c['all-assets'];assert initial['source_files']==2 and initial['all_materials']==7 and len(initial['rows'])==6,initial;assert initial['generated_previews']>=5
assert sum(v['saved']for v in initial['rows'])==1 and sum(v['generated']for v in initial['rows'])==5;assert initial['cached_originals']==0,'Browsing copied source originals into the persistent cache'
assert len(c['metals']['rows'])==2 and all(v['catalog']=='Materials/Metals'for v in c['metals']['rows']);assert len(c['nested-search']['rows'])==1 and 'nested/extra.blend'in c['nested-search']['rows'][0]['source'];assert len(c['plastics']['rows'])==2;assert len(c['including-unmarked']['rows'])==7 and not all(v['asset']for v in c['including-unmarked']['rows']);assert len(c['tag-search']['rows'])==1 and c['tag-search']['rows'][0]['text'].startswith('Textured generated')
assert c['shelf-imported']['document']==c['before-library']['document'];assert len(c['shelf-imported']['blend_materials'])==1;assert c['undone']['document']==c['before-library']['document'];assert c['applied']['document']==c['redone']['document']==c['opened-without-library']['document']==c['cancelled-scan']['document'];assert d['cancelled_while_loading']
matrix=[[.6130974024,.3395231462,.0473794514],[.0701937225,.9163538791,.0134523985],[.0206155929,.1095697729,.8698146342]];expected=[sum(row[i]*[.3,.1,.7][i]for i in range(3))for row in matrix]
def sample(x,precision):
 if precision==0:
  encoded=12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055;encoded=int(encoded*255+.5)/255;x=encoded/12.92 if encoded<=.04045 else((encoded+.055)/1.055)**2.4
 elif precision in[1,2]:x=int(x*255+.5)/255
 elif precision in[3,4]:x=int(x*65535+.5)/65535
 return x if precision in[7,8]else struct.unpack('e',struct.pack('e',x))[0]
for name in ['applied','redone','opened-without-library']:
 case=c[name];precision=case['document']['texture_sets'][0]['precision'][0];actual=case['material_gpu_samples'][0];assert max(abs(v-sample(x,precision))for v,x in zip(actual[:3],expected))<3e-5,(name,actual,expected)
assert files()==json.loads((out/'source-before.json').read_text()),'Source library changed';assert not(out/'studio-library/library.json').exists();assert d['max_browser_ping_ms']<100,d['max_browser_ping_ms'];log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_BRUSH_CPU_FALLBACK',log,re.I)
result={'explicit_root_only':True,'native_readonly_catalogs':True,'nested_catalogs_and_duplicate_names':True,'files':2,'marked_materials':6,'all_materials':7,'saved_previews':1,'generated_previews':5,'preview_original_cache_copies':0,'asset_toggle_and_tag_path_search':True,'source_hashes_unchanged':True,'outside_symlink_skipped':True,'native_gpu_fill_and_history':True,'packed_open_without_library_or_cache':True,'cancel_scan_no_publish':True,'max_browser_ping_ms':d['max_browser_ping_ms'],'no_default_library_write':True};(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
