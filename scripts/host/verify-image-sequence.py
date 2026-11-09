# SPDX-License-Identifier: GPL-3.0-or-later
"""Native UDIM grouping from one file, per-channel tile match and packed Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--check-existing',action='store_true',help='Validate an already captured native run without launching another application');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'sequence.obj';lines=['mtllib sequence.mtl','o SequencePlanes']
for tile in range(3):
 left=tile*1.2-1.6
 for x,y in [(left,-.5),(left+1,-.5),(left+1,.5),(left,.5)]:lines.append(f'v {x} {y} 0')
for tile in range(3):
 for u,v in [(.05,.05),(.95,.05),(.95,.95),(.05,.95)]:lines.append(f'vt {tile+u} {v}')
lines+=['vn 0 0 1','usemtl SequenceSurface']
for tile in range(3):lines.append('f '+' '.join(f'{tile*4+i}/{tile*4+i}/1' for i in range(1,5)))
mesh.write_text('\n'.join(lines)+'\n');mesh.with_suffix('.mtl').write_text('newmtl SequenceSurface\nKd 1 1 1\nPr .4\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_PLANAR_DEMO='1',BTS_IMAGE_SEQUENCE_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
pid=None
if not a.check_existing:
 with (a.output/'run.log').open('w') as log:
  process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=240)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
 assert code==0,code
 pid=process.pid
proof=json.loads((a.output/'image-sequence.json').read_text());assert pid is None or proof['pid']==pid;pid=proof['pid'];c={v['stage']:v for v in proof['captures']}
def projection(stage):return c[stage]['document']['texture_sets'][0]['layers'][-1]['fill_projection']
def tiles(stage):return {v['tile']:v for v in c[stage]['sequence_tiles']}
def hashes(stage):return [v['hash'] for v in c[stage]['sequence_tiles']]
for v in c.values():assert v['decimal']=='.' and not v['shader_failures'] and v['document']['schema']==79 and not v['property_pending']
assert c['created']['document']==c['imported-only']['document']
assert c['imported-only']['sequence_shelf']==[dict(name='PD103-color',count=2)]
assert projection('match-uniform')['mode']==6
for stage in ['match-uniform','matched-color','matched-data','matching-ignores-transform','opened-packed']:
 assert not c[stage]['transforms_visible'] and not c[stage]['filter_visible'] and not c[stage]['frame_visible'] and not c[stage]['panel_frame_visible'] and not c[stage]['spatial_disclosure_visible']
 assert c[stage]['document']['texture_sets'][0]['tiles']==[1001,1002,1003]
color=tiles('matched-color');assert color[1001]['color'][0]>color[1001]['color'][1] and color[1002]['color'][1]>color[1002]['color'][0]
assert color[1001]['hash']!=color[1002]['hash'] and color[1003]['color']==[0,0,0,0]
resource=projection('matched-color')['texture'];assert resource['name']=='PD103-color' and len(resource['tiles'])==2
assert resource['tiles'][0]['image']['width']==64 and resource['tiles'][0]['image']['height']==32
assert resource['tiles'][1]['image']['width']==32 and resource['tiles'][1]['image']['height']==64
for tile,value in [(1001,64/255),(1002,192/255)]:assert abs(tiles('matched-data')[tile]['roughness'][0]-value)<.0008
assert tiles('matched-data')[1003]['roughness']==[0,0,0,0]
assert projection('matched-data')['channel_textures'][1]['raw'] and len(projection('matched-data')['channel_textures'][1]['tiles'])==2
ordinary=tiles('ordinary-uv');assert max(abs(a-b) for a,b in zip(ordinary[1001]['color'],ordinary[1002]['color']))<1e-6
assert c['ordinary-uv']['transforms_visible'] and c['ordinary-uv']['frame_visible']
assert projection('matching-ignores-transform')['scale']==[2,2] and projection('matching-ignores-transform')['offset']==[.2,0]
assert hashes('matching-ignores-transform')==hashes('matched-data')==hashes('redo-mode')
assert projection('undo-mode')['mode']==0 and projection('redo-mode')['mode']==6
assert projection('roughness-uniform')['channel_textures'][1] is None
uniform_set=c['roughness-uniform']['document']['texture_sets'][0];assert uniform_set['precision'][2]==1
uniform_value=uniform_set['layers'][-1]['values']['roughness'];quantized_uniform=int(uniform_value*255+.5)/255
assert all(abs(v['roughness'][0]-quantized_uniform)<1e-6 for v in tiles('roughness-uniform').values())
assert projection('undo-uniform')['channel_textures'][1] is not None
assert hashes('undo-uniform')==hashes('reference')
assert [v['roughness'] for v in tiles('undo-uniform').values()]==[v['roughness'] for v in tiles('reference').values()]
assert c['reference']['composite_checked'] and not c['reference']['composite_differences'],c['reference']['composite_differences']
assert c['saved']['document']==c['opened-packed']['document']
assert hashes('saved')==hashes('opened-packed')==hashes('undo-open')
assert [v['roughness'] for v in tiles('saved').values()]==[v['roughness'] for v in tiles('opened-packed').values()]
assert hashes('color-uniform-open')!=hashes('opened-packed')
assert not projection('atom-matched')['physical'] and projection('atom-matched')['atom']
assert len(set(hashes('atom-matched')))==1
assert c['atom-matched']['thumbnail_hash']!=c['atom-uv']['thumbnail_hash']
assert hashes('atom-matched')==hashes('atom-restored')
assert not c['final']['composite_differences'],c['final']['composite_differences']
resources=[v for v in c['opened-packed']['sequence_resources'] if v['name'].startswith('PD103')];assert len(resources)==2 and all(v['sequence_count']==2 for v in resources)
assert all(not (a.output/name).exists() for name in ['PD103-color_1001.png','PD103-color_1002.png','PD103-data_1001.png','PD103-data_1002.png'])
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={pid}' in log
result=dict(pid=pid,locale='sv_SE.UTF-8',one_file_groups_neighboring_tiles=True,import_leaves_layers_unchanged=True,shelf_count_badge=2,per_tile_color=True,rectangular_sources=True,raw_red_data=True,missing_tile='transparent layer; lower layers/base remain visible',match_hides_mapping_controls=True,ordinary_slots_use_first_tile=True,match_ignores_retained_transforms=True,atom_and_thumbnail_tile_match=True,undo_redo=True,packed_open_without_source_files=True,exact_async_sync_composite=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
