# SPDX-License-Identifier: GPL-3.0-or-later
"""Offscreen production Qt Shelf widgets; no Blender scene or display connection."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
expected=['All','Project','Alphas','Grunges','Procedurals','Textures','Hard Surfaces','Skin','Filters','Brushes','Particles','Tools','Materials','Smart materials','Smart masks','Environments','Color profiles','Flattened layers','Baked']
results={}
for locale in ['sv_SE','en_US']:
 out=a.output/locale;out.mkdir(parents=True,exist_ok=True)
 if not a.existing:
  env=dict(os.environ,LANG=locale+'.UTF-8',LC_ALL=locale+'.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_SHELF_NAV_TEST='1',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'),XDG_CACHE_HOME=str(out/'cache'),BTS_RECOVERY_DIR=str(out/'recovery'),BTS_SESSION_LOG_DIR=str(out/'logs'))
  env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
  with (out/'run.log').open('w') as log:
   subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--ac-ui-capture-demo',str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
 proof=json.loads((out/'shelf-navigation.json').read_text());assert proof['locale']==locale
 assert [c['category']for c in proof['categories']]==expected
 assert all(c['query_cleared']and c['reset_disabled']for c in proof['categories'])
 for key in ['project_scope','shelf_locations_readonly_in_open_project','baked_project_scope_and_sort','user_scope_and_reload','session_scope','chip_removal','layer_selection_unchanged','resource_selection_remembered','skin_thematic_across_types','no_resource_applied_by_browsing']:assert proof[key],key
 results[locale]=proof
(a.output/'verified.json').write_text(json.dumps({'production_qt_widgets':True,'display_connection':False,'results':results},indent=2)+'\n');print('Shelf navigation: 19 categories, chips, scopes, query/reset, themes, selection and user-library reload PASS in sv_SE/en_US')
