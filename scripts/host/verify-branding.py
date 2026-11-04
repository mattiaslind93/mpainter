"""Approved mPainter branding on an owned Xvfb / RTX session, never the desktop."""
import argparse,json,os,statistics,subprocess,hashlib
from pathlib import Path
from PIL import Image
from private_gpu_gui import private_gpu_gui
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8');binary=bts/'build/host/bin/bts-host'
with private_gpu_gui(out,env,root)as(prefix,env):
 with(out/'unit.log').open('w')as log:subprocess.run(prefix+[str(binary.with_name('bts-branding-tests')),str(out/'unit')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 alternate=out/'alternate.png';Image.new('RGB',(1400,781),(20,70,210)).save(alternate);alternate.with_suffix('.focus').write_text('0.25 0.5\n')
 results=[]
 for name,disabled,featured in [('warmup',True,None),('normal-1',False,None),('without-1',True,None),('normal-2',False,None),('without-2',True,None),('normal-3',False,None),('without-3',True,None),('alternate',False,alternate),('hidpi',False,None)]:
  directory=out/name;directory.mkdir(exist_ok=True);run_env=dict(env,BTS_BRANDING_CAPTURE=str(directory),BTS_BRANDING_QUIT='1');run_env.pop('MPAINTER_SPLASH_IMAGE',None)
  if name=='hidpi':run_env['QT_SCALE_FACTOR']='2'
  if featured:run_env['MPAINTER_SPLASH_IMAGE']=str(featured)
  command=prefix+[str(binary),'--workspace']+(['--no-splash']if disabled else [])
  with(directory/'run.log').open('w')as log:subprocess.run(command,env=run_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
  data=json.loads((directory/'branding.json').read_text());assert data['display_name']=='mPainter'and data['internal_name']=='Blender Texture Studio'and data['title']=='mPainter'and data['version']=='0.0.1'and data['icon']and data['splash_closed'],data
  assert(directory/'workspace.png').is_file();assert(directory/'splash.png').is_file()!=disabled
  data['run']=name;results.append(data)
 first=Image.open(out/'normal-1/splash.png').convert('RGB');changed=Image.open(out/'alternate/splash.png').convert('RGB');assert first.size==changed.size==(1200,600);assert changed.getpixel((1000,300))==(20,70,210);assert first.getpixel((1000,300))!=changed.getpixel((1000,300));assert first.crop((0,0,400,500)).tobytes()==changed.crop((0,0,400,500)).tobytes();assert Image.open(out/'hidpi/splash.png').size==(2400,1200)
 med=lambda prefix:statistics.median(r['startup_ms']for r in results if r['run'].startswith(prefix))
 summary={'feature':'MATTIAS-FIX AT','version':'0.0.1','private_renderer':json.loads((out/'private-display.json').read_text()),'startup':{'without_splash_median_ms':med('without'),'with_splash_median_ms':med('normal'),'difference_ms':med('normal')-med('without'),'max_compose_ms':max(r['splash_compose_ms']for r in results)},'runs':results,'approved_overlay_unchanged':True,'separate_featured_swap_verified':True,'hidpi_unit_2x':True,'window_icon':True,'legacy_settings_identity_preserved':True,'font_licenses':['Quicksand OFL 1.1','Righteous OFL 1.1']}
 assert summary['startup']['max_compose_ms']<150,summary['startup'];assert summary['startup']['difference_ms']<150,summary['startup']
 (out/'verified.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
