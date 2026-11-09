"""sv_SE whole-workspace numeric display and comma input, private offscreen only."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();assert out.is_relative_to(bts.resolve()/'build');out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,LC_ALL='sv_SE.UTF-8',LANG='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_NUMERIC_LOCALE_DEMO='1')
for name,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('TMPDIR','tmp')]:
 (out/folder).mkdir(exist_ok=True);env[name]=str(out/folder)
if not a.existing:
 with(out/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(out/'studio-library'),'--autosave-seconds','3600'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=150,check=True)
for source in (root/'native/host').rglob('*'):
 if source.suffix in ('.cc','.hh') and 'Demo'not in source.name:
  assert 'new QDoubleSpinBox'not in source.read_text(),source
d=json.loads((out/'numeric-workspace.json').read_text());assert d['system_locale']=='sv_SE'and d['ui_locale']=='C'and d['comma_value']==.25 and d['decimal']=='.';assert not d['shader_failures'];assert len(d['numbers'])>100
for name in ['brushSize','toolbarBrushFlow','brushSpacing','brushMinimumFlow','lazyMouseDistance']:
 assert next(v['text']for v in d['numbers']if v['name']==name).endswith(' %'),name
for row in d['numbers']:
 assert ','not in row['text'],row
 if '%'in row['text']:assert re.search(r'\d+\.\d{2} %$',row['text']),row
# Only decimal-looking scalar text. Coordinates, JSON and human list separators
# are deliberately not interpreted as decimal numbers or rewritten.
bad=[v for v in d['texts']if re.search(r'(?<![\w,.])[-+]?\d+,\d+(?![\d,.])(?=\s*(?:%|cm|mm|m|°|px)?(?:\s|$))',re.sub(r'<[^>]*>','',v['text']))];assert not bad,bad
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log and not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result={'locale':'sv_SE.UTF-8','ui_locale':'C','numeric_widgets':len(d['numbers']),'widget_texts_scanned':len(d['texts']),'decimal_comma_widgets':0,'comma_input_value':d['comma_value'],'percent_fixed_2_decimals':True,'blender_numeric_locale':d['decimal'],'clean_exit':True};(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
