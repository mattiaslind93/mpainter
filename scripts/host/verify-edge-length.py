"""Native PD-121 object-space Edge Length approximation, with original-source checks."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.existing:
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_EDGE_LENGTH_DEMO='1',BTS_DISPLACEMENT_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'))
 with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/displacement_cube.obj'),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
proof=json.loads((a.output/'edge-length.json').read_text());caps={c['stage']:c for c in proof['captures']};base=caps['level0'];original=(a.output/'level0.obj').read_bytes()
def active(name):return [o for o in caps[name]['geometry']['objects']if o['preview_modifiers']]
for name,level,kind in [('level0',0,1),('uniform',2,1),('catmull',2,0),('edge-default',6,1),('edge-half',1,1),('edge-quarter',2,1),('undo-edge',1,1),('redo-edge',2,1),('opened',2,1)]:
 objects=active(name);assert objects and all(o['level']==level and o['subdivision_type']==kind and o['uv_smooth']==0 for o in objects),(name,objects)
 assert any(m['method']==2 and m['linked'] and m['content']and m['subdivision_level']==level for m in caps[name]['geometry']['materials']),(name,caps[name]['geometry']['materials'])
for name,c in caps.items():
 assert (a.output/(name+'.obj')).read_bytes()==original,name
 assert c['source_mesh_hash']==base['source_mesh_hash'],name
 if not name.startswith('uv-'):assert c['picked_uv']==base['picked_uv'],name
 assert [o['source_uv_hash']for o in c['geometry']['objects']]==[o['source_uv_hash']for o in base['geometry']['objects']],name
 assert c['decimal']=='.'and not c['shader_failures']
 image=Image.open(a.output/(name+'.png')).convert('RGB');assert max(ImageStat.Stat(image).stddev)>2,('blank frame',name)
 if name not in ['opened','defaults','undo-defaults']:assert c['view']['scene_view_matrix']==base['view']['scene_view_matrix'],('camera moved',name)
for name in ['edge-default','edge-half','edge-quarter','undo-edge','redo-edge','opened']:
 c=caps[name];assert c['displacement']['mode']==1 and c['displacement']['subdivision']==2 and not c['count_enabled']and c['max_length_enabled'],name
assert caps['disabled']['displacement']['mode']==1 and caps['disabled']['displacement']['max_length']==.25
assert not caps['disabled']['count_enabled']and not caps['disabled']['max_length_enabled']and not active('disabled')
for name in ['edge-default','edge-half','edge-quarter','undo-edge','redo-edge','disabled','enabled','saved','opened']:assert caps[name]['geometry']['edge_scans']==1,(name,caps[name]['geometry']['edge_scans'])
assert caps['saved']['displacement']==caps['opened']['displacement']
assert caps['defaults']['displacement']['mode']==0 and caps['defaults']['displacement']['strength']==1 and caps['defaults']['displacement']['max_length']==1/64
assert caps['undo-defaults']['displacement']==caps['opened']['displacement']
for name in ['uv-catmull','uv-edge']:assert ImageChops.difference(Image.open(a.output/'uv-uniform.png'),Image.open(a.output/(name+'.png'))).getbbox()is None,name
assert [o['evaluated_geometry_hash']for o in active('uniform')]==[o['evaluated_geometry_hash']for o in active('edge-quarter')]
differences={}
for before,after in [('uniform','catmull'),('edge-half','edge-default'),('edge-quarter','disabled')]:
 delta=ImageChops.difference(Image.open(a.output/(before+'.png')).convert('RGB'),Image.open(a.output/(after+'.png')).convert('RGB'));differences[before+'/'+after]=sum(ImageStat.Stat(delta).mean)/765;assert differences[before+'/'+after]>1e-5
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_STACK_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',log,re.I)
result={'pid':proof['pid'],'stages':len(caps),'mode':'Object-space uniform Simple approximation; not adaptive EEVEE tessellation','default_max_length':1/64,'levels':{'default':6,'half':1,'quarter':2},'uv_no_smoothing':True,'source_geometry_picking_and_obj_unchanged':True,'effective_bake_density_available':True,'one_longest_edge_scan_per_source_generation':True,'master_enable_retains_fields':True,'undo_redo_save_open_defaults':True,'image_differences':differences,'clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
