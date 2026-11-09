# SPDX-License-Identifier: GPL-3.0-or-later
"""Ten GUI workflows with opt-in thread CPU, asynchronous GPU queries and transfer counters."""
import argparse,json,os,re,statistics,subprocess
from contextlib import nullcontext
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--private-xvfb',action='store_true');p.add_argument('--perf',action='store_true');p.add_argument('--apitrace',action='store_true');p.add_argument('--reference',action='store_true');p.add_argument('--resolution',type=int,default=1024);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if a.private_xvfb:
 if a.perf or a.apitrace:p.error('--private-xvfb cannot wrap --perf or --apitrace')
 if not Path('/run/.containerenv').exists() or os.getpriority(os.PRIO_PROCESS,0)!=19:p.error('--private-xvfb requires bts-build and nice 19')
 if not a.output.is_relative_to(root.parent/'build'):p.error('--private-xvfb requires private output under BTS build')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_PERFORMANCE_PROFILE='1')
# Preserve the caller's display/runtime selection. The default can run without
# touching a desktop, and application state is private to this verification.
env.setdefault('QT_QPA_PLATFORM','offscreen')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resource-cache'),('BTS_SESSION_LOG_DIR','logs')]:
 directory=a.output/folder;directory.mkdir(mode=0o700,exist_ok=True);env[key]=str(directory)
if not env.get('XDG_RUNTIME_DIR'):
 directory=a.output/'runtime';directory.mkdir(mode=0o700,exist_ok=True);env['XDG_RUNTIME_DIR']=str(directory)
if a.perf and a.apitrace:p.error('Use separate timing and GL audit runs')
if a.reference:env.update(BTS_EXPORT_REFERENCE='1',BTS_COMPOSITE_SERIAL='1',BTS_COMPOSITE_SAMPLED='1',BTS_GPU_UNIFORM_DISABLE='1',BTS_GPU_STACK_DISABLE='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution',str(a.resolution),'--recovery-dir',str(a.output/'recovery')]
if a.apitrace:cmd=['apitrace','trace','--api','gl','--output',str(a.output/'native.trace')]+cmd
if a.perf:cmd=['perf','record','-e','cpu-clock','-F','99','--call-graph','dwarf,8192','-o',str(a.output/'perf.data'),'--']+cmd
if a.private_xvfb:
 from private_gpu_gui import private_gpu_gui
 gui=private_gpu_gui(a.output,env,root)
else:gui=nullcontext(([],env))
with gui as (prefix,private):
 with (a.output/'run.log').open('w') as f:
  process=subprocess.Popen(prefix+cmd,env=private,stdout=f,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=900)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-2500:])
proof=json.loads((a.output/'performance.json').read_text());assert len(proof['cases'])==10
rows=[]
for case in proof['cases']:
 stages=[s for s in proof['stages'] if s['flow']==case['flow']];gpu=[s['wall_ms'] for s in stages if s['phase']=='gpu_render'];draw=[s['wall_ms'] for s in stages if s['phase']=='draw'];queue=[s['queue_ms'] for s in stages if s['phase'].endswith('_queue')]
 row={k:case[k] for k in ('flow','wall_ms','process_cpu_ms','action_return_ms','max_ping_ms','settled_after_release_ms','transfers')};row.update(gpu_median_ms=statistics.median(gpu) if gpu else None,gpu_max_ms=max(gpu,default=0),draw_median_ms=statistics.median(draw) if draw else None,queue_max_ms=max(queue,default=0),ui_stages_cpu_ms=sum(s['cpu_ms'] for s in stages if s['ui']),worker_stages_cpu_ms=sum(s['cpu_ms'] for s in stages if not s['ui']));rows.append(row)
assert proof['final']['decimal']=='.' and not proof['final']['shader_failures'];assert proof['final']['composite_checked'] and not proof['final']['composite_differences']
log=(a.output/'run.log').read_text();assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK',log,re.I);assert 'BTS_SHUTDOWN clean' in log
result=dict(pid=proof['pid'],resolution=a.resolution,cases=rows,exact_final_composite=True);(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');
for row in rows:print(f"{row['flow']}: wall={row['wall_ms']:.1f}ms CPU={row['process_cpu_ms']:.1f}ms GPU={row['gpu_median_ms'] or 0:.2f}ms ping={row['max_ping_ms']:.1f}ms upload={row['transfers']['upload']['bytes']/1048576:.2f}MiB")
