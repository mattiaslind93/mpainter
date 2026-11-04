"""Real Qt/RTX overlay profile on an authorized copy, owned Xvfb only."""
import argparse,json,math,os,statistics,subprocess
from pathlib import Path
from PIL import Image
from private_gpu_gui import private_gpu_gui
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--project',type=Path);p.add_argument('--label',default='before');p.add_argument('--existing',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to((bts/'build').resolve());out.mkdir(parents=True,exist_ok=True);project=(a.project or bts/'build/z-baseline/project.blend').resolve();assert project.is_relative_to((bts/'build').resolve())and project.is_file()
env=dict(os.environ,BTS_OVERLAY_PROFILE='1',BTS_PERFORMANCE_PROFILE='1',LC_ALL='sv_SE.UTF-8',LANG='sv_SE.UTF-8');binary=bts/'build/host/bin/bts-host'
if a.label!='before':env.update(BTS_OVERLAY_TRIANGLE_PROFILE='1',BTS_OVERLAY_SELF_TEST_DIR=str(out))
if not a.existing:
 with private_gpu_gui(out,env,root)as(prefix,env):
  with(out/'run.log').open('w')as log:subprocess.run(prefix+[str(binary),'--workspace','--open-document',str(project),'--gui-fix-demo',str(out),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(out/'library'),'--autosave-seconds','3600'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
d=json.loads((out/'overlay-performance.json').read_text())
def stats(v):
 v=sorted(v);return {'count':len(v),'p50_ms':statistics.median(v)if v else 0,'p95_ms':v[max(0,math.ceil(.95*len(v))-1)]if v else 0,'max_ms':max(v,default=0)}
rows=[]
for capture in d['captures']:
 phase=capture['phase'];records=[r for r in d['phases']if r['flow']=='av-'+phase];row=dict(capture)
 for name in ['viewport_full_frame','viewport_chrome','polygon_overlay_build','polygon_overlay_draw','uv_contours_overlay','gpu_render','gpu_overlay']:
  row[name]=stats([r['wall_ms']for r in records if r['phase']==name])
 row['ping']=stats([r['ms']for r in d['pings']if r['phase']==phase]);rows.append(row)
 assert row['viewport_full_frame']['count']>=20,(phase,row)
 for name in ['readback','finish']:
  before=capture['transfers_before'][name];after=capture['transfers_after'][name];row[name+'_calls']=after['calls']-before['calls'];row[name+'_bytes']=after['bytes']-before['bytes']
assert rows[1]['overlayTriangles']>100000 and (rows[1]['polygonLines0']or rows[1]['native_wire_vbos'])>0,rows[1]
assert rows[2]['uvContourEdges']>0 and rows[3]['polygonLines1']>0,rows[3]
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log and 'BTS_DRAW_FAILED'not in log
result={'feature':'MATTIAS-FIX AV','label':a.label,'project_copy':str(project),'private_renderer':json.loads((out/'private-display.json').read_text()),'rows':rows,'metric_scope':'CPU full frame includes QPainter chrome; GPU render and GPU overlay are nonblocking timestamp queries in their respective owner contexts. Screenshots are taken outside measured flows.','clean_shutdown':True}
if a.label!='before':
 result['gpu_self_test']=json.loads((out/'gpu-overlay-self-test.json').read_text());assert len(rows)==6;assert rows[4]['triangle_vbo_builds']==1 and rows[5]['uv_vbo_builds']==3;assert rows[1]['native_wire_cache_changes']==rows[1]['native_wire_vbos']
 result['visible_wire_pixels']={}
 for name in ('scene-bucket','scene-triangles','uv-bucket','uv-triangles'):
  pixels=Image.open(out/(name+'.png')).convert('RGB');count=sum(r>180 and r>g*1.25 and g>b*1.3 for r,g,b in pixels.getdata());assert count>1000,(name,count);result['visible_wire_pixels'][name]=count
 assert result['gpu_self_test']['occluded_pixels']==0 and result['gpu_self_test']['production_depth_readbacks']==0
 assert all(r['polygon_overlay_build']['count']==0 and r['finish_calls']==0 for r in rows)
 assert all(r['viewport_full_frame']['p95_ms']<16.7 for r in rows),rows

(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
