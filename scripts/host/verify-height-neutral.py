# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed artist Bump midgray, selectable displacement neutral, native GPU/history/Open."""
import argparse,json,os,re,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to(bts.resolve()/'build');out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_HEIGHT_NEUTRAL_DEMO='1',BTS_SKEW_DEMO='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
for name,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('TMPDIR','tmp')]:
 (out/folder).mkdir(exist_ok=True);env[name]=str(out/folder)
if not a.existing:
 with(out/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(out/'studio-library'),'--autosave-seconds','3600'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((out/'height-neutral.json').read_text());c={v['stage']:v for v in d['captures']}
for name in ['created','fill-neutral','bump-flat','disp-zero','disp-gray','redone','opened']:
 assert abs(c[name]['height_values']['bump']-.5)<1e-6,(name,c[name]['height_values'])
assert c['bump-raised']['document']['texture_sets'][0]['precision'][4]==5
assert c['bump-raised']['height_values']['bump']==struct.unpack('e',struct.pack('e',.8))[0]
for name,mid in [('created',0),('fill-neutral',0),('disp-zero',0),('disp-gray',.5),('redone',.5),('opened',.5)]:assert c[name]['displacement']['midlevel']==mid,(name,c[name]['displacement'])
assert c['opened']['document']==c['redone']['document']==c['disp-gray']['document'];assert c['undone']['document']!=c['redone']['document']
for name in ['fill-neutral','bump-flat']:
 normal=c[name]['height_values']['normal'];assert max(abs(a-b)for a,b in zip(normal[:3],[.5,.5,1]))<1e-5,(name,normal)
assert all(not v['shader_failures']and v['decimal']=='.'for v in c.values());log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log and not re.search(r'BTS_BRUSH_CPU_FALLBACK|BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed',log,re.I)
result={'bump_fixed_midgray':True,'bump_gui_and_gpu':True,'flat_normal':True,'displacement_default_zero':True,'displacement_midgray_option':True,'undo_redo_and_packed_open':True,'locale':'sv_SE.UTF-8','stages':list(c)};(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
