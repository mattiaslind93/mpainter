# SPDX-License-Identifier: GPL-3.0-or-later
"""Native W regression, invisible Qt workspace on the RTX offscreen surface.

All sources are synthetic and all preferences/recovery/cache paths are isolated.
No input device or window is created on the artist's desktop.
"""
import argparse,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--large',action='store_true');p.add_argument('--extra-channels',action='store_true');p.add_argument('--cpu-only',action='store_true');p.add_argument('--gui-only',action='store_true');p.add_argument('--locale',default='sv_SE.UTF-8');a=p.parse_args()
root=Path(__file__).resolve().parents[2];bts=root.parent;a.output=a.output.resolve();assert a.output.is_relative_to(bts/'build'), 'Test output must be in BTS build';a.output.mkdir(parents=True,exist_ok=True)
(a.output/'tmp').mkdir(exist_ok=True)
env=dict(TMPDIR=str(a.output/'tmp'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_ROOT=str(bts),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_PERFORMANCE_PROFILE='1',BTS_SESSION_PROFILE='1',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_RESOURCE_CACHE=str(a.output/'resource-cache'),BTS_TEXTURE_IMPORT_FIXTURES=str(a.output/'fixtures'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),LANG=a.locale,LC_ALL=a.locale)
if a.large:env['BTS_TEXTURE_IMPORT_LARGE']='1'
if a.extra_channels:env['BTS_TEXTURE_IMPORT_EXTRA_CHANNELS']='1'
runner=root/'scripts/host/run-host.sh'
def run(label,args,extra=None):
 command=(['env'] if Path('/run/.containerenv').exists() else ['distrobox', 'enter', 'bts-build', '--', 'env'])+[key+'='+value for key,value in (env|dict(extra or {})).items()]+['nice','-n','19','bash',str(runner)]+args
 with (a.output/(label+'.log')).open('w')as log:
  result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 assert result.returncode==0,(label,result.returncode,(a.output/(label+'.log')).read_text()[-5000:])
if not a.gui_only:
 run('cpu',['--cpu-texture-import-test',str(a.output/'fixtures')])
 cpu=json.loads((a.output/'fixtures/texture-import-cpu.json').read_text());assert cpu['per_tile_pack_exact'] and cpu['metadata_ap1_identity'] and cpu['data_ignores_tag']
if not a.cpu_only:
 run('workspace',['--workspace','--gui-fix-demo',str(a.output/'workspace'),'--gui-fix-mesh',str(root/'native/tests/fixtures/two_tile_udim.obj'),'--gui-fix-resolution','2048' if a.large else '512'],{'BTS_TEXTURE_IMPORT_DEMO':'1'})
 gui=json.loads((a.output/'workspace/texture-import-gui.json').read_text());captures={c['stage']:c for c in gui['captures']};assert gui['sources_absent'];assert captures['color-loaded']['fill_mode']==6
 for tile in captures['color-loaded']['sequence_tiles']:
  assert abs(tile['color'][0]-(.25 if tile['tile']==1001 else .75))<.004,tile
 for tile in captures['roughness-loaded']['sequence_tiles']:
  assert abs(tile['roughness'][0]-(.25 if tile['tile']==1001 else .75))<.006,tile
 assert captures['roughness-loaded']['shader_failures']==''
 assert captures['corrupt-unchanged']['fill_texture_hash']==captures['roughness-loaded']['fill_texture_hash']
 assert captures['cancelled-unchanged']['fill_texture_hash']==captures['shelf-bound']['fill_texture_hash']==captures['missing-unchanged']['fill_texture_hash']
 assert 'alpha_decodes=0 file_references=2' in (a.output/'workspace.log').read_text()
 print(json.dumps({key:value for key,value in captures.items()if key.endswith('latency')},indent=2))
print('Native texture formats, raw/color UDIM and per-tile packed sources PASS')
