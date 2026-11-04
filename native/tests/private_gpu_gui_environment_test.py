"""Real private-prefix admission regression; no BTS/Qt app or desktop input.

Run explicitly inside bts-build, nice19, with a new private build output path.
The owned gpu-env shim models variables reintroduced by a login profile.
The real prefix and real session guard are used for both controls.
"""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts/host'))
from private_gpu_gui import private_gpu_gui
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS,0)==19
assert len(sys.argv)==2
out=Path(sys.argv[1]).resolve();assert out.is_relative_to(root.parent/'build') and not out.exists();out.mkdir()
env={k:v for k,v in os.environ.items() if not k.startswith(('BTS_','XDG_','QT_','BLENDER_')) and k not in ('DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD')}
env.update(BTS_ROOT=str(root.parent),BTS_ENVIRONMENT=str(root.parent/'build/shared-fixtures/TT_environmentLights_v001.hdr'))
shim=out/'profile-model/scripts/host/gpu-env.sh';shim.parent.mkdir(parents=True)
shim.write_text('source "'+str(root/'scripts/host/gpu-env.sh')+'"\nexport WAYLAND_DISPLAY=owned-regression-wayland\nexport XAUTHORITY='+str(out/'owned-dummy-authority')+'\n')
child=out/'check.py';child.write_text('''import json,os,sys
sys.path.insert(0,sys.argv[1])
from session_guard import owned_private_xvfb
print(json.dumps({'wayland_present':bool(os.environ.get('WAYLAND_DISPLAY')),'xauthority_present':bool(os.environ.get('XAUTHORITY')),'owned':owned_private_xvfb(caller_pid=os.getpid()),'display':os.environ.get('DISPLAY'),'platform':os.environ.get('QT_QPA_PLATFORM')}))
''')
results={}
with private_gpu_gui(out/'private',env,shim.parents[2]) as (prefix,environment):
 for name,command in [('current',prefix),('negative-old',prefix[:2]+[prefix[2].replace('unset WAYLAND_DISPLAY XAUTHORITY; ','')]+prefix[3:])]:
  result=subprocess.run(command+['python3',str(child),str(root/'scripts/host')],env=environment,capture_output=True,text=True,timeout=30)
  assert result.returncode==0,(name,result.returncode,result.stderr)
  results[name]=json.loads(result.stdout)
assert results['current']['owned'] and not results['current']['wayland_present'] and not results['current']['xauthority_present'],results
assert not results['negative-old']['owned'] and results['negative-old']['wayland_present'] and results['negative-old']['xauthority_present'],results
assert results['current']['display']==results['negative-old']['display'] and results['current']['platform']=='xcb',results
receipt={'actual_exit':0,'results':results,'real_prefix':True,'real_guard':True,'no_native_application':True,'no_desktop_input':True,'profile_injection_model':True,'private_helper_sha256':hashlib.sha256((root/'scripts/host/private_gpu_gui.py').read_bytes()).hexdigest()}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
