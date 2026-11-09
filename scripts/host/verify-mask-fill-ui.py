# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared fill/mask widgets and deterministic mask mapping; isolated offscreen Qt."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=Path.cwd()/'mask-fill-ui');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent))
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_MASK_FILL_UI_TEST='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery')]:
 (a.output/folder).mkdir(exist_ok=True);env[key]=str(a.output/folder)
env['LD_LIBRARY_PATH']=str(bts/'qt/6.8.3/lib')+':'+env.get('LD_LIBRARY_PATH','');env['QT_PLUGIN_PATH']=str(bts/'qt/6.8.3/plugins')
with(a.output/'run.log').open('w')as log:subprocess.run([str(bts/'build/host/bin/bts-host'),'--ac-ui-capture-demo',str(a.output/'panels')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
d=json.loads((a.output/'panels/mask-fill-ui.json').read_text());assert d['single_grayscale_row']==1 and d['visible_material_buttons']==0;assert d['drag_started']==d['drag_finished']==1
for key in ['scale_changes_samples','rotation_changes_samples','triplanar_changes_samples','legacy_clamp_retained']:assert d[key]
print(json.dumps(d,indent=2))
