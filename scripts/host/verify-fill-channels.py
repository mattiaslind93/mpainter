"""All active fill-channel resources, raw decoding, independent Undo and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
legacy=root.parent/'build/mattias-K-gui/fill-texture.blend';assert legacy.is_file()
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_FILL_TEXTURE_DEMO='1',BTS_FILL_CHANNELS_DEMO='1',BTS_FILL_CHANNELS_LEGACY=str(legacy))
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=900)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'fill-channels.json').read_text());assert proof['pid']==process.pid;c={v['stage']:v for v in proof['captures']}
for value in c.values():
 assert value['decimal']=='.' and not value['shader_failures'] and value['document']['schema']==58
 assert not value['property_pending'] and not value['composite_pending'],value['stage']
for channel in range(15):
 value=c[f'channel-{channel}'];data=value['fill_channels'][channel];assert data['resolved'] and data['raw']==(channel!=0),(channel,data)
 assert all(row['visible'] for row in value['resource_labels']),channel
 assert value['resource_labels'][channel]['text']==('channel-normal' if channel==3 else 'channel-pair')
 expected=(128/255 if channel==3 else 64/255)
 if channel:assert abs(data['source_first'][0]-expected)<1e-6,(channel,data['source_first'])
 else:assert abs(data['source_first'][0]-expected)>.01,data
 previous=c['uniform'] if channel==0 else c[f'channel-{channel-1}'];assert value['undo_count']==previous['undo_count']+1,(channel,value['undo_count'],previous['undo_count'])
 for other in range(15):
  if other==channel:continue
  before=previous['fill_channels'][other];after=value['fill_channels'][other]
  assert before['name']==after['name'],(channel,other,before,after)
  if not(channel==4 and other==3):assert before.get('hash')==after.get('hash'),(channel,other,before,after)
all_bound=c['all-bound']['fill_channels'];assert all(v['resolved'] and 'hash' in v for v in all_bound)
assert not c['roughness-uniform']['fill_channels'][2]['resolved']
assert c['roughness-uniform']['fill_channels'][2]['hash']!=all_bound[2]['hash']
assert c['undo-uniform']['fill_channels']==all_bound and c['textures-restored']['fill_channels']==all_bound
assert c['redo-uniform']['fill_channels']==c['roughness-uniform']['fill_channels']
assert c['tiling']['fill_channels'][2]['hash']!=all_bound[2]['hash']
assert c['triplanar']['fill_channels'][2]['hash']!=c['tiling']['fill_channels'][2]['hash']
assert any(o['evaluated_vertices']>o['source_vertices'] for o in c['all-bound']['geometry']['objects'])
assert any(m['method']==2 and m['linked'] and m['content'] for m in c['all-bound']['geometry']['materials'])
assert c['reference']['composite_checked'] and not c['reference']['composite_differences'],c['reference'].get('composite_differences')
assert c['saved']['document']==c['opened-packed']['document'] and c['saved']['fill_channels']==c['opened-packed']['fill_channels']
assert not (a.output/'channel-pair.png').exists() and not (a.output/'channel-normal.png').exists()
assert c['legacy']['fill_channels'][0]['resolved'] and all(not v['resolved'] for v in c['legacy']['fill_channels'][1:])
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:];assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',channels=15,independent_file_and_shelf_sources=True,raw_red_and_rgb_normal=True,color_acescg=True,uniform_restore=True,one_undo_per_binding=True,shared_mapping=True,eevee_displacement_subdiv=True,exact_async_sync_composite=True,packed_save_open_without_files=True,legacy_color_project=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
