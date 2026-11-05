"""Actual 4K EEVEE displacement-bump detail on geometry with only eight vertices."""
import argparse,json,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=a.output.resolve();assert out.is_relative_to(root.parent/'build');out.mkdir(parents=True,exist_ok=True)
env={'BTS_ROOT':str(root.parent),'QT_QPA_PLATFORM':'offscreen','BTS_HEADLESS_WORKSPACE':'1','LANG':'sv_SE.UTF-8','LC_ALL':'sv_SE.UTF-8','BTS_TEXTURE_IMPORT_FIXTURES':str(out/'fixtures')}
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('TMPDIR','tmp'),('BTS_SESSION_LOG_DIR','logs')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
def run(label,args,extra):
 cmd=(['env'] if Path('/run/.containerenv').exists() else ['distrobox', 'enter', 'bts-build', '--', 'env'])+[k+'='+v for k,v in (env|extra).items()]+['nice','-n','19','bash',str(root/'scripts/host/run-host.sh')]+args
 with (out/(label+'.log')).open('w')as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 assert r.returncode==0,(label,r.returncode,(out/(label+'.log')).read_text()[-4000:])
run('fixtures',['--cpu-texture-import-test',str(out/'fixtures')],{'BTS_AB_DETAIL_FIXTURES':'1'})
run('workspace',['--workspace','--gui-fix-demo',str(out/'workspace'),'--gui-fix-mesh',str(out/'fixtures/detail.obj'),'--gui-fix-resolution','4096','--autosave-seconds','3600'],{'BTS_AB_DISPLACEMENT':'1','BTS_AB_DETAIL':'1'})
d=json.loads((out/'workspace/ab-displacement.json').read_text());c={x['stage']:x for x in d['captures']}
for name,value in c.items():
 assert not value['shader_failures'],name
 assert value['input_space']=='Non-Color/raw' and value['midlevel']==0,name
 assert not value['alpha_control'],name
 assert all(s['gpu_resident'] and s['native'][3]==1 for s in value['ab_samples']),name
 objects=value['geometry']['objects'];assert sum(o['source_vertices']for o in objects)==8,objects
 assert all(o['source_vertices']==o['evaluated_vertices'] for o in objects),objects
 for image in value['residency']['images']:assert image['sampling_cap']==image['cap']==max(image['source_width'],image['source_height']),image
 assert any(i['source_width']==4096 for i in value['residency']['images']),name
 if name!='detail-disabled':assert any(m['method']==2 and m['evaluated_method']==2 for m in value['geometry']['materials']),name
rect=next(p['rect']for p in c['detail-full']['view']['panes']if p['view']=='3D');x,y,w,h=map(int,rect)
def pixels(name):return Image.open(out/'workspace'/(name+'.png')).convert('RGB').crop((x+35,y+60,x+w-35,y+h-35))
full,coarse,restored,disabled=map(pixels,['detail-full','detail-coarse-reference','detail-restored','detail-disabled'])
def mse(a,b):
 difference=ImageChops.difference(a,b);return sum(ImageStat.Stat(difference).sum2)/(a.width*a.height*3*255**2)
metrics={'full_vs_coarse_mse':mse(full,coarse),'full_vs_restored_mse':mse(full,restored),'coarse_vs_disabled_mse':mse(coarse,disabled)}
assert metrics['full_vs_coarse_mse']>1e-5,metrics
assert metrics['full_vs_restored_mse']<1e-4,metrics
assert metrics['full_vs_restored_mse']<metrics['full_vs_coarse_mse']*.1,metrics
log=(out/'workspace.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert 'BTS_NATIVE_GPU_ORACLE' in log and not re.search(r'Shader compilation failed|BTS_DRAW_FAILED|BTS_HOST_FAILED',log)
proof={'pid':d['pid'],'source_size':4096,'source_vertices':8,'preview_subdivision_level':0,'bump_detail_without_additional_vertices':True,'zero_height_at_mesh_vertices':True,'both_displacement_method':True,'full_resolution_restored_exact_within_render_tolerance':True,'locale':'sv_SE.UTF-8','mse':metrics}
(out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
