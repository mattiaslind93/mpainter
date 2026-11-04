"""GUI New, shared mask mapping/resource edits and actual native GPU pixel oracle."""
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
image=a.output/'ao-checker.png';im=Image.new('RGBA',(32,32));im.putdata([(51 if(x//4+y//4)%2 else 204,)*3+(0,)for y in range(32)for x in range(32)]);im.save(image)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_MASK_FILL_GPU_DEMO='1',BTS_MASK_FILL_IMAGE=str(image),LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources')]:
 (a.output/folder).mkdir(exist_ok=True);env[key]=str(a.output/folder)
args=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','512','--library-dir',str(a.output/'library'),'--autosave-seconds','3600']
with(a.output/'run.log').open('w')as log:subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
d=json.loads((a.output/'mask-fill-gpu.json').read_text());assert [c['stage']for c in d['captures']]==['atom','scaled','rotated','triplanar','horizontal','texture','alpha-masked','uniform'];assert d['scale_drag_undo']==1
texture=next(c for c in d['captures']if c['stage']=='texture');assert max(p[3]for p in texture['samples'])>.1, 'Zero file alpha must not hide raw grayscale'
log=(a.output/'run.log').read_text();oracles=re.findall(r'BTS_NATIVE_GPU_ORACLE channels=(\d+) probes=(\d+) tiles=(\d+) max_error=(\S+)',log);assert len(oracles)>=8 and 'BTS_SHUTDOWN clean'in log
assert 'BTS_BRUSH_CPU_FALLBACK'not in log and 'BTS_FINALIZE_FALLBACK'not in log
assert re.search(r'BTS_TEXTURE_LOAD[^\n]*raw=1[^\n]*Non-Color/raw',log), 'Mask images must decode as raw grayscale, not display color'
masked=next(c for c in d['captures']if c['stage']=='alpha-masked');assert all(p[3]==0 for p in masked['samples']), 'Explicit alpha coverage must hide this zero-alpha fixture'
d['native_oracles']=[dict(channels=int(c),probes=int(n),tiles=int(t),max_error=float(e))for c,n,t,e in oracles];(a.output/'verified.json').write_text(json.dumps(d,indent=2)+'\n');print('Shared mask fill: 8 native GPU/CPU comparisons, visible mapping/resource edits and one Undo PASS')
