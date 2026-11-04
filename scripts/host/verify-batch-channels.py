"""PD142: native channel dialog, cancellation, scope, single-step Undo and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,shutil,subprocess
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
for name in ['two_materials.obj','two_materials.mtl']:shutil.copyfile(root/'replica/specs/fixtures'/name,a.output/name)
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_BATCH_CHANNEL_DEMO=str(a.output),BTS_BATCH_CHANNEL_MESH=str(a.output/'two_materials.obj'))
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=240)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-6000:])
d=json.loads((a.output/'channels.json').read_text());c={s['stage']:s for s in d['captures']};assert len(c)==8
if not a.check_only:assert d['pid']==process.pid
def sets(stage):return c[stage]['document']['texture_sets']
def config(s):return (s['channels'],s['precision'],s['ao_channel'],s['displacement']['channel'],s['displacement']['precision'],[(u['present'],u['precision'])for u in s['users']])
assert sets('before')[0]['channels'][1] and not sets('before')[1]['channels'][1]
assert c['before']['document']==c['cancel']['document'] and c['before']['undo']==c['cancel']['undo']
assert sets('local')[1]==sets('before')[1]
assert not sets('local')[0]['channels'][1] and sets('local')[0]['ao_channel'] and sets('local')[0]['displacement']['channel']
assert sets('local')[0]['precision'][2]==5
assert config(sets('all')[0])==config(sets('all')[1]) and not sets('all')[0]['channels'][4]
assert c['local']['undo']==c['before']['undo']+1 and c['all']['undo']==c['local']['undo']+1
assert c['local']['document']==c['undo']['document']
assert c['all']['document']==c['redo']['document']
assert c['saved']['document']==c['opened']['document']
for left,right in [('local','undo'),('all','redo'),('saved','opened')]:
 assert c[left]['textures']==c[right]['textures'],(left,right)
for stage in c:
 assert not c[stage]['shader_failures'],stage
 image=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');assert not any(r>235 and g<20 and b>235 for r,g,b in image.getdata()),stage
assert 'BTS_SHUTDOWN clean' in (a.output/'run.log').read_text()
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','stages':list(c),'initial_sets_differ':True,'cancel_unchanged':True,'single_set_isolated':True,'apply_all_presence_and_precision':True,'one_undo_per_apply':True,'undo_redo_native_hashes_exact':True,'packed_open_exact':True,'magenta_pixels':0}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
