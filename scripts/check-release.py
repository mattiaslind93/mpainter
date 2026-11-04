#!/usr/bin/env python3
"""Run focused branding, theme and bundled-asset regression checks after building them."""
import argparse,os,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',required=True,type=pathlib.Path);a=p.parse_args()
suffix='.exe' if os.name=='nt' else ''
with tempfile.TemporaryDirectory(prefix='release-check-',dir=a.build_dir) as temp:
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',XDG_CONFIG_HOME=temp+'/config',XDG_DATA_HOME=temp+'/data',XDG_CACHE_HOME=temp+'/cache')
    for name in ['bts-branding-tests','bts-asset-tests','bts-theme-tests']:
        subprocess.run([str(a.build_dir/'bin'/(name+suffix))],env=env,check=True)
subprocess.run([os.sys.executable,str(pathlib.Path(__file__).parent/'host/check-theme-colors.py')],check=True)
print('Branding, theme and bundled-asset regression checks passed.')
