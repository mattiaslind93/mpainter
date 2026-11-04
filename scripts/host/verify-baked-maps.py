"""Real two-UDIM bake -> named Shelf previews -> bitmap mask -> native GPU and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_BAKED_MAPS_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:
 subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/two_tile_udim.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'baked-maps.json').read_text());c={x['stage']:x for x in d['captures']};maps=c['baked-shelf']['maps']
assert {x['role']for x in maps}=={3,4,5,6}
for item in maps:
 tiles=item['tiles'];assert {x['tile']for x in tiles}=={1001,1002}
 assert all(x['covered_texels']>100 and len(x['probes'])==3 for x in tiles)
 assert tiles[0]['hash']!=tiles[1]['hash'],item
shelf=c['baked-shelf']['shelf'];assert len(shelf)==4
for item in shelf:
 assert item['icon'] and ' – 'in item['name'] and '1001, 1002'in item['tooltip'] and '128 × 128'in item['tooltip'],item
assert any('Ambient Occlusion'in x['name']for x in shelf)
for label in ['mask-applied','position-mask','reopened']:
 cap=c[label];assert cap['mask_mode']==6 and cap['mask_raw'] and len(set(cap['mask_storage_keys']))==2
 ao=next(x for x in cap['maps']if x['role']==cap['source_role'])
 for tile in ao['tiles']:
  for probe in tile['probes']:
   assert probe['on_mesh']
   assert abs(probe['gpu_coverage']-probe['cpu_mask'])<.0005,(label,tile,probe)
assert c['reopened']['maps']==c['position-mask']['maps']
position=next(x for x in c['position-mask']['maps']if x['role']==5)
assert abs(position['tiles'][0]['probes'][0]['baked']-position['tiles'][1]['probes'][0]['baked'])>.05
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
assert not re.search(r'BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'real_bake_roles':['Ambient Occlusion','Curvature','Position','Thickness'],'nonempty_distinct_udim_tiles':[1001,1002],'shelf_named_thumbnails_and_resolution_tooltips':True,'bitmap_mask_cpu_and_native_gpu_per_tile':True,'distinct_gpu_resource_identity_per_tile':True,'packed_open':True,'captures':list(c),'maps':maps}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
