"""Native Clone/Smudge controls, Undo, packed reopen and GPU-only authoring.
The process logger is permitted; all Blender/Qt owner markers must share one PID.
"""
import argparse,json,os,re,signal,subprocess
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--mode',choices=['clone','smudge','both'],default='both');p.add_argument('--existing',action='store_true');args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
def verify(mode,out):
    a=SimpleNamespace(output=out,clone=mode=='clone',smudge=mode=='smudge');result={}
    log=(out/'run.log').read_text();demo=json.loads((out/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid'])
    assert f'BTS_SHUTDOWN clean pid={process.pid}' in log and 'Not freed memory blocks' not in log
    for marker in ['BTS_SCENE_READY','BTS_GPU_READY','BTS_FRAME','BTS_NEW_PROJECT']:
        entries=re.findall(marker+r'[^\n]*?pid=(\d+)',log);assert entries and all(int(pid)==process.pid for pid in entries),marker
    assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_BRUSH_CPU_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',log),mode
    if a.clone:
        demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
        captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'entered', 'missing-source', 'sampled-world', 'relative3d', 'undo', 'redo', 'absolute', 'resampled-world', 'copy1', 'copy2', 'undo-copy2', 'redo-copy2', 'paint-restored', 'clone-restored', 'rough-disabled', 'sampled-uv', 'color-only', 'undo-color', 'rough-enabled', 'saved', 'reopened', 'entered-open', 'missing-open', 'sampled-open', 'clone-open', 'undo-open']
        assert list(captures)==stages
        def data(stage):return captures[stage]['clone']
        def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
        def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
        first=data('entered');assert first['active'] and not first['absolute'] and not first['source_selected'] and first['size']==10 and first['flow']==100 and first['spacing']==20 and first['channels']==[True]*5
        for stage in ['entered','relative3d','absolute','copy1','copy2','clone-restored','entered-open','clone-open']:
            d=data(stage);assert d['active'] and not d['rough_values_visible'] and 'No material' in d['title'],stage
        assert data('sampled-world')['source_selected'] and data('sampled-uv')['source_selected']
        for before,after in [('setup','entered'),('entered','missing-source'),('missing-source','sampled-world'),('setup','undo'),('relative3d','redo'),('absolute','resampled-world'),('copy1','undo-copy2'),('copy2','redo-copy2'),('redo-copy2','paint-restored'),('paint-restored','clone-restored'),('rough-disabled','sampled-uv'),('redo-copy2','undo-color'),('saved','reopened'),('entered-open','missing-open'),('missing-open','sampled-open'),('reopened','undo-open')]:
            for key in ['hashes','native_hash','rough_hash','operations','undo_count']:
                if (before,after)==('saved','reopened') and key=='undo_count':continue
                assert data(before)[key]==data(after)[key],(before,after,key)
        for before,after in [('sampled-world','relative3d'),('resampled-world','copy1'),('copy1','copy2'),('sampled-uv','color-only'),('sampled-open','clone-open')]:
            assert data(before)['hashes'][0]!=data(after)['hashes'][0] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
            assert data(after)['undo_count']==data(before)['undo_count']+1 and data(after)['undo_label']=='Clone stroke',(before,after)
        assert data('relative3d')['operations']==data('setup')['operations']+1
        for stage,key in [('copy1','copy1'),('copy2','copy2')]:
            pixel=data(stage)[key];assert pixel[3]>.2 and all(abs(pixel[c]/pixel[3]-1)<1e-5 for c in range(3)),(stage,pixel)
        assert data('clone-restored')['absolute'] and data('clone-restored')['size']==20 and 'absolute' in data('clone-restored')['mode_label']
        assert not data('paint-restored')['active'] and data('paint-restored')['size']==18 and data('paint-restored')['spacing']==100
        assert data('rough-disabled')['hashes'][2]==data('color-only')['hashes'][2] and data('rough-disabled')['rough_hash']==data('color-only')['rough_hash']
        assert not data('entered-open')['source_selected']
        for stage in stages[1:]:
            assert other(stage)==other('setup') and len(target(stage)['layers'])==len(target('setup')['layers']),stage
            for key in ['source','neighbor','blue']:assert data(stage)[key]==data('setup')[key],(stage,key)
            for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        result['clone_demo']={'pid':process.pid,'stages':stages,'v_click_source_does_not_edit_document':True,'relative_world_stroke_and_two_absolute_uv_copies':True,'no_material_values_and_independent_profile':True,'source_neighbors_other_texture_set_unchanged':True,'disabled_roughness_retained':True,'one_gesture_undo_redo_and_packed_open_further_clone':True,'source_session_cleared_on_open':True,'scope':'Selected raw layer; relative source follows per-gesture displacement; no lower-stack/Passthrough or Painter exact projection parity claim.'}
    if a.smudge:
        demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
        captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','entered','smudged3d','undo','redo','paint-restored','smudge-restored','smudged-uv','undo-uv','redo-uv','rough-disabled','color-only','undo-color','rough-enabled','saved','reopened','entered-open','smudge-open','undo-open','empty-layer','empty-smudge','empty-click']
        assert list(captures)==stages
        def data(stage):return captures[stage]['smudge']
        def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
        def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
        first=data('entered');assert first['active'] and first['size']==10 and first['flow']==50 and first['spacing']==3 and first['opacity']==100 and first['alpha']=='Shape' and first['hardness']==0 and first['pressure_size'] and first['minimum_size']==5 and first['angle']==0 and not first['follow'] and first['channels']==[True]*5
        for stage in ['entered','smudged3d','undo','smudge-restored','smudged-uv','entered-open','smudge-open','empty-smudge']:
            d=data(stage);assert d['active'] and d['flow']==50 and d['spacing']==3 and not d['rough_values_visible'] and 'No material' in d['title'],stage
        assert data('smudged3d')['tail']>.01 and data('setup')['tail']==0
        assert data('smudged3d')['white_error']<1e-5 and data('smudged3d')['rough_error']<1e-5,data('smudged3d')
        assert data('smudged3d')['undo_count']==data('entered')['undo_count']+1 and data('smudged3d')['undo_label']=='Smudge stroke'
        assert data('smudged3d')['operations']==data('setup')['operations']+2 and data('smudged3d')['spatial']==data('setup')['spatial']+2
        for before,after in [('entered','smudged3d'),('smudge-restored','smudged-uv'),('rough-disabled','color-only'),('entered-open','smudge-open')]:
            assert data(before)['hashes'][0]!=data(after)['hashes'][0] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
        for before,after in [('setup','entered'),('setup','undo'),('smudged3d','redo'),('redo','paint-restored'),('paint-restored','smudge-restored'),('smudged3d','undo-uv'),('smudged-uv','redo-uv'),('redo-uv','rough-disabled'),('smudged-uv','undo-color'),('undo-color','rough-enabled'),('smudged-uv','saved'),('saved','reopened'),('reopened','undo-open')]:
            for key in ['hashes','native_hash','rough_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
        assert not data('paint-restored')['active'] and data('paint-restored')['size']==18 and data('paint-restored')['spacing']==100 and data('paint-restored')['alpha']=='Sign Plus'
        assert not data('rough-disabled')['channels'][2] and data('rough-enabled')['channels'][2]
        assert data('rough-disabled')['hashes'][2]==data('color-only')['hashes'][2] and data('rough-disabled')['rough_hash']==data('color-only')['rough_hash']
        assert data('reopened')['loaded'] and data('reopened')['operations']==data('saved')['operations']
        for before,after in [('empty-layer','empty-smudge'),('empty-smudge','empty-click')]:
            assert captures[before]['document']==captures[after]['document'],(before,after)
            for key in ['hashes','native_hash','rough_hash','undo_count','redo_count','operations']:assert data(before)[key]==data(after)[key],(before,after,key)
        for stage in stages[1:]:
            assert other(stage)==other('setup'),stage
            assert len(target(stage)['layers'])==len(target('setup')['layers'])+(stages.index(stage)>=20),stage
            if stages.index(stage)<20:
                assert data(stage)['neighbor']==data('setup')['neighbor'] and data(stage)['blue']==data('setup')['blue'],stage
            for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
            if stages.index(stage)<16:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        result['smudge_demo']={'pid':process.pid,'stages':stages,'selected_layer_raw_transport_fading_white_tail':data('smudged3d')['tail'],'white_and_roughness_source_errors':[data('smudged3d')['white_error'],data('smudged3d')['rough_error']],'no_material_values_and_independent_profile':True,'one_gesture_undo_with_two_replayable_spatial_patch_operations':True,'uv_transport_and_disabled_roughness_retention':True,'neighbor_other_mark_and_texture_set_unchanged':True,'empty_layer_does_not_sample_lower_layers':True,'packed_open_further_smudge_and_undo':True,'scope':'Own brush-payload transport of selected raw layer; no Painter numerical-kernel/lower-layer sampling parity claim.'}

    backend=re.search(r'BTS_GPU_TRANSPORT_VERIFIED cases=(\d+).*cpu_reads=0',log);assert backend and int(backend[1])>=140
    assert 'BTS_GPU_CAPTURE_LOCALITY_VERIFIED source_pages=256 staged_pages=4' in log
    recorded=len(re.findall(r'BTS_BRUSH_GPU_RECORD',log));assert recorded>=4,(mode,recorded);assert re.search(r'BTS_BRUSH_GPU_RECORD stamps=\d+ tool='+mode,log),mode
    probes=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log);assert len(probes)>20 and all(c[2]=='1' for c in probes),mode
    return dict(stages=len(demo['captures']),gpu_recorded_batches=recorded,gpu_backend_cases=int(backend[1]),gpu_stack_checks=len(probes),maximum_stack_probe_error=max(float(c[1]) for c in probes),cpu_authoring_reads=0,cpu_composition_fallbacks=0,undo_packed_reopen_exact=True,controls=result)
results_path=args.output/'transport-gpu.json';results=json.loads(results_path.read_text()) if results_path.exists() else {}
for mode in (['clone','smudge'] if args.mode=='both' else [args.mode]):
    out=args.output/mode;out.mkdir(parents=True,exist_ok=True)
    if not args.existing:
        fixture=out/'material-projection.obj'
        fixture.write_text('mtllib material-projection.mtl\no ProjectionPlane\nusemtl A_ProjectionSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\no OtherPlane\nusemtl Z_OtherSurface\nv 1.1 -.2 0\nv 1.5 -.2 0\nv 1.5 .2 0\nv 1.1 .2 0\nf 5/1 6/2 7/3 8/4\n')
        fixture.with_suffix('.mtl').write_text('newmtl A_ProjectionSurface\nKd .5 .5 .5\nnewmtl Z_OtherSurface\nKd .3 .3 .3\n')
        env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',XDG_CONFIG_HOME=str(out/'config'),XDG_DATA_HOME=str(out/'data'),BTS_RESOURCE_CACHE=str(out/'resources'),BTS_RECOVERY_DIR=str(out/'recovery'))
        command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(args.environment),'--new-mesh',str(args.mesh),'--new-demo-dir',str(out),'--'+mode+'-demo',str(fixture),'--recovery-dir',str(out/'recovery')]
        with (out/'run.log').open('w') as log:
            child=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            try:code=child.wait(timeout=240)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=5)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
                raise
        assert code==0,mode
    results[mode]=verify(mode,out)
    results_path.write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
