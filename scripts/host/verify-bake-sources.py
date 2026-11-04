"""Native Alembic/USD high-poly, custom cage, frame and unit regression."""
import argparse,json,os,subprocess
from pathlib import Path
from private_gpu_gui import private_gpu_gui
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');p.add_argument('--low-usd',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to((bts/'build').resolve());out.mkdir(parents=True,exist_ok=True);binary=bts/'build/host/bin/bts-host'
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_BAKE_SOURCE_DEMO='1',BTS_SKEW_DEMO='1')
for key,name in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs'),('TMPDIR','tmp')]:
 (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
env.update(QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+env.get('LD_LIBRARY_PATH',''))
if not a.existing:
 if not(out/'fixtures/high-cm.usdc').exists():
  with(out/'fixtures.log').open('w')as log:subprocess.run([str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(root/'scripts/host/generate-bake-source-fixtures.py'),'--',str(out/'fixtures')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
 with(out/'unit.log').open('w')as log:subprocess.run([str(binary.with_name('bts-bake-source-tests')),str(out/'unit')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with(out/'cpu.log').open('w')as log:subprocess.run([str(binary),'--cpu-bake-source-test',str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
 with private_gpu_gui(out,env,root)as(prefix,private):
  with(out/'run.log').open('w')as log:subprocess.run(prefix+[str(binary),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(out/('fixtures/low.usdc'if a.low_usd else 'fixtures/low.abc')),'--gui-fix-resolution','128','--library-dir',str(out/'library'),'--autosave-seconds','3600'],env=private,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
cpu=json.loads((out/'bake-sources.json').read_text());data=json.loads((out/'bake-source-gui.json').read_text());c={v['stage']:v for v in data['captures']}
assert all(not v['shader_failures']and v['decimal']=='.'for v in c.values())
for name in ['baked-abc','baked-usdc','baked-frame2','baked-abc-cage','opened']:
 sets=c[name]['document']['texture_sets'];assert len(sets)==1 and sets[0]['tiles']==[1001,1002],(name,sets)
 maps=sets[0]['mesh_maps'];assert len(maps)==7
 for role in [0,3,4]:assert len(maps[role]['tiles'])==2,(name,role,maps[role])
assert c['opened']['document']==c['baked-abc-cage']['document'],'Packed Open changed mesh maps or source settings'
config=c['opened']['document']['bake_configuration'];assert config['high_files']==[str(out/'fixtures/high.usdc')]and config['cage_file']==str(out/'fixtures/cage.abc')and config['use_cage']and not config['source_frame_enabled']and config['source_scale']==1,config
frame=c['baked-frame2']['document']['bake_configuration'];assert frame['source_frame_enabled']and frame['source_frame']==2
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log and 'BTS_HOST_FAILED'not in log
result={'feature':'MATTIAS-FIX AU','low_format':'USDC'if a.low_usd else 'Alembic','locale':'sv_SE.UTF-8','native_cpu':cpu,'native_gui_stages':list(c),'two_udim_low_alembic_and_usd':True,'gui_alembic_usdc_high_and_alembic_cage':True,'saved_source_paths_and_maps_exact':True,'frame_controls_and_units':True,'private_renderer':json.loads((out/'private-display.json').read_text())};(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
