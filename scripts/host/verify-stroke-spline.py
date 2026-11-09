# SPDX-License-Identifier: GPL-3.0-or-later
"""Sparse curved mouse/pen input, immediate tail and frame-independent raster proof."""
from parity_test_environment import private_display
import argparse,json,math,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_STROKE_SPLINE_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','1024','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=300)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'stroke-spline.json').read_text());assert proof['pid']==proc.pid and len(proof['runs'])==4

def segment_distance(point,a,b):
 dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy;t=max(0,min(1,((point[0]-a[0])*dx+(point[1]-a[1])*dy)/length)) if length else 0
 return math.dist(point,[a[0]+t*dx,a[1]+t*dy])
rows=[]
for burst,oracle in zip(proof['runs'][::2],proof['runs'][1::2]):
 assert not burst['oracle'] and oracle['oracle']
 assert burst['hash']==oracle['hash'] and burst['bump_hash']==oracle['bump_hash']
 for run in (burst,oracle):
  assert run['hash']!=run['before']['hash'] and run['undo_count']==run['before']['undo_count']+1
  assert run['input_points']-run['before']['input_points']==9,run['input_points']-run['before']['input_points']
  assert run['compress_mouse'] and run['compress_tablet'] and not run['contact']['compress_mouse'] and not run['contact']['compress_tablet']
  assert run['contact']['painting'] and not run['painting'] and run['contact']['spline_dabs']==run['spline_dabs']
  assert run['decimal']=='.' and not run['shader_failures']
  assert run['reference']['composite_checked'] and not run['reference']['composite_differences']
 dabs=burst['spline_dabs'];assert len(dabs)>30
 deviation=max(min(segment_distance(d['point'],x,y) for x,y in zip(burst['inputs'],burst['inputs'][1:])) for d in dabs)
 assert deviation>2,deviation
 end_gap=math.dist(dabs[-1]['point'],burst['inputs'][-1]);assert end_gap<5,end_gap
 assert burst['max_ping_ms']<100 and burst['penup_ms']<100,(burst['max_ping_ms'],burst['penup_ms'])
 if burst['input']=='tablet':
  assert burst['last_dab_diameter']>burst['first_dab_diameter']*3
  assert max(abs(x['pressure']-y['pressure']) for x,y in zip(dabs,dabs[1:]))<.1
  assert max(abs(x['tilt'][c]-y['tilt'][c]) for x,y in zip(dabs,dabs[1:]) for c in (0,1))<4
 rows.append(dict(input=burst['input'],dabs=len(dabs),max_ping_ms=burst['max_ping_ms'],penup_upper_bound_ms=burst['penup_ms'],max_curve_deviation_from_chords_px=deviation,end_gap_px=end_gap,all_input_points=True,predicted_tail_already_visible_at_contact=True,exact_unbatched_raster=True,single_undo=True))
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_STROKE_INPUT|BTS_STROKE_SPLINE|Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-3000:]
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',brush_size=float(env.get('BTS_SPLINE_BRUSH_SIZE','1')),runs=rows,physical_wacom=False);(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
