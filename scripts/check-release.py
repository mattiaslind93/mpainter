#!/usr/bin/env python3
"""Run focused branding and empty-library regression checks after building them."""
import argparse,os,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',required=True,type=pathlib.Path);a=p.parse_args()
suffix='.exe' if os.name=='nt' else ''
with tempfile.TemporaryDirectory(prefix='release-check-',dir=a.build_dir) as temp:
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',XDG_CONFIG_HOME=temp+'/config',XDG_DATA_HOME=temp+'/data',XDG_CACHE_HOME=temp+'/cache')
    for name in ['bts-branding-tests','bts-asset-tests']:
        subprocess.run([str(a.build_dir/'bin'/(name+suffix))],env=env,check=True)
print('Branding and empty-library regression checks passed.')
