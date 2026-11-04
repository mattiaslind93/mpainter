"""GPU-recorded polygon and editable brush paths; private native Qt workspace."""
import argparse,json,os,re,subprocess
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--mode',choices=['polygon','surface_path'],required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(a.output),'--recovery-dir',str(a.output/'recovery')]
if a.mode=='polygon':command+=['--polygon-demo']
else:
 fixture=a.output/'material-projection.obj'
 fixture.write_text('mtllib material-projection.mtl\no ProjectionPlane\nusemtl A_ProjectionSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\no OtherPlane\nusemtl Z_OtherSurface\nv 1.1 -.2 0\nv 1.5 -.2 0\nv 1.5 .2 0\nv 1.1 .2 0\nf 5/1 6/2 7/3 8/4\n')
 fixture.with_suffix('.mtl').write_text('newmtl A_ProjectionSurface\nKd .5 .5 .5\nnewmtl Z_OtherSurface\nKd .2 .2 .2\n')
 command+=['--surface-path-demo',str(fixture)]
if not a.existing:
 with (a.output/'run.log').open('w')as log:completed=subprocess.run(command,env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_VERIFY_GPU_STACK='1'),stdout=log,stderr=subprocess.STDOUT,timeout=300)
 assert completed.returncode==0,completed.returncode
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}'in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|BTS_FINALIZE_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK|BTS_GPU_AUTHORING_CPU_READ|BTS_BRUSH_CPU_FALLBACK',log)
if a.mode=='polygon':
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','triangle-mode','triangle','polygon3d','island','mesh','black-island','undo-black','redo-black','rectangle','undo-rectangle','redo-rectangle','saved','reopened','material','painted','undo-paint']
    def stats(stage):return captures[stage]['polygon']
    assert stats('initial')['active'] and stats('initial')['mode']==1 and stats('initial')['grayscale']==1
    assert captures['triangle-mode']['document']==captures['initial']['document']
    assert stats('initial')['white']==0 and stats('triangle')['white']>0
    assert stats('polygon3d')['white']>stats('triangle')['white']
    assert stats('island')['white']>stats('polygon3d')['white']
    assert stats('mesh')['white']==stats('mesh')['triangles']
    assert 0<stats('black-island')['white']<stats('mesh')['white']
    assert stats('rectangle')['white']==stats('mesh')['white']
    for before,after in [('mesh','undo-black'),('black-island','redo-black'),('black-island','undo-rectangle'),('rectangle','redo-rectangle'),('saved','reopened'),('saved','material'),('saved','undo-paint')]:assert captures[before]['document']==captures[after]['document'],(before,after)
    assert captures['painted']['document']!=captures['material']['document']
    assert not stats('material')['active'] and stats('material')['brush_visible']
    first=captures['initial'];errors={}
    for stage,c in captures.items():
        p=c['polygon'];assert p['modes']==[p['mode']]*2
        assert abs(p['grayscale_toolbar']-p['grayscale'])<1e-6 and abs(p['grayscale_properties']-p['grayscale'])<1e-6
        if p['active']:assert not p['picker_enabled'] and not p['brush_visible']
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1]
        if stage not in ['reopened','material','painted','undo-paint']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
    result['polygon_demo']={'pid':process.pid,'stages':stages,'four_native_geometry_fill_modes':True,'black_island_overwrites_white':True,'rectangle_is_one_undo_transaction':True,'controls_synchronized_picker_disabled':True,'save_open_and_continued_mask_painting':True,'other_texture_set_unchanged':True,'presentation_errors':errors}
if a.mode=='surface_path':
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'entered', 'pending', 'created', 'extended', 'finished', 'uv-rejected', 'renamed', 'hidden', 'shown', 'duplicated', 'deleted', 'undo-delete', 'saved', 'reopened', 'entered-open', 'reselected', 'extended-open', 'undo-open', 'fx-selected', 'fx-created', 'fx-extended', 'fx-saved', 'fx-reopened', 'fx-reselected', 'fx-extended-open', 'fx-undo-open', 'sphere-setup', 'sphere-created', 'sphere-saved', 'sphere-reopened']
    assert list(captures)==stages
    def data(stage):return captures[stage]['surface_path']
    def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']==data(stage)['target'])
    def paths(stage):return data(stage)['paths']
    assert data('entered')['active'] and data('entered')['panel_visible'] and not paths('entered')
    assert data('pending')['pending'] and not paths('pending') and data('pending')['hashes']==data('setup')['hashes']
    assert len(paths('created'))==1 and paths('created')[0]['points']==2 and paths('extended')[0]['points']==3
    assert not data('finished')['editing'] and not data('finished')['pending']
    assert captures['finished']['document']==captures['uv-rejected']['document']
    assert paths('renamed')[0]['name']=='Seam trace'
    assert not paths('hidden')[0]['visible'] and data('hidden')['hashes']==data('setup')['hashes']
    assert paths('shown')[0]['visible'] and data('shown')['hashes']==data('extended')['hashes']
    assert len(paths('duplicated'))==2 and paths('duplicated')[0]['id']!=paths('duplicated')[1]['id']
    assert data('duplicated')['hashes'][0]!=data('shown')['hashes'][0]
    assert len(paths('deleted'))==1 and data('deleted')['hashes']==data('shown')['hashes']
    for before,after in [('duplicated','undo-delete'),('undo-delete','saved'),('saved','reopened'),('reopened','undo-open'),('fx-saved','fx-reopened'),('fx-reopened','fx-undo-open'),('sphere-created','sphere-saved'),('sphere-saved','sphere-reopened')]:
        for key in ['hashes','native_hash','paths']:assert data(before)[key]==data(after)[key],(before,after,key)
    assert not data('reopened')['active'] and not data('reopened')['panel_visible'] and data('reopened')['history_loaded']
    assert paths('extended-open')[0]['points']==4 and paths('undo-open')[0]['points']==3
    assert data('fx-selected')['effect'] and not paths('fx-selected')
    assert paths('fx-created')[0]['points']==2 and paths('fx-extended')[0]['points']==3 and paths('fx-extended-open')[0]['points']==4
    assert data('fx-reopened')['history_loaded'] and data('fx-created')['base_layer_hash']==data('fx-extended-open')['base_layer_hash']
    assert paths('sphere-created'),data('sphere-created')
    assert paths('sphere-created')[0]['points']==2 and paths('sphere-created')[0]['normal_dot']<.999,paths('sphere-created')
    for before,after in [('pending','created'),('created','extended'),('reselected','extended-open'),('fx-selected','fx-created'),('fx-created','fx-extended'),('fx-reselected','fx-extended-open'),('sphere-setup','sphere-created')]:
        assert data(before)['hashes'][0]!=data(after)['hashes'][0] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
        assert data(after)['undo_count']==data(before)['undo_count']+1,(before,after)
    for stage in stages[1:]:
        baseline='setup' if stages.index(stage)<28 else 'sphere-setup'
        assert len(target(stage)['layers'])==len(target(baseline)['layers']) and data(stage)['old_mark']==data(baseline)['old_mark'],stage
        other='Z_OtherSurface' if baseline=='setup' else 'CubeSurface'
        assert next(t for t in captures[stage]['document']['texture_sets'] if t['name']==other)==next(t for t in captures[baseline]['document']['texture_sets'] if t['name']==other),stage
    result['surface_path_demo']={'pid':process.pid,'stages':stages,'native_mesh_surface_contacts_drive_path_ink':True,'plane_and_curved_sphere_paths':True,'pending_first_point_and_uv_rejection_leave_document_unchanged':True,'paths_panel_rename_visibility_duplicate_delete_with_undo':True,'layer_and_paint_effect_paths_remain_editable_after_packed_open':True,'old_mark_other_texture_set_and_layer_count_preserved':True,'scope':'Modern documentation contract; own viewport panel and built-in paint alphas, no Painter9+ pixel-perfect UI claim.'}
result['desktop_presentation_checked']=False
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
