"""Native continuous ribbon images, path edits, mapping controls, Undo and portable .blend Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'ribbon-plane.obj';mesh.write_text('mtllib ribbon-plane.mtl\no RibbonSurface\nusemtl RibbonSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n');mesh.with_suffix('.mtl').write_text('newmtl RibbonSurface\nKd .12 .12 .12\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_RIBBON_DEMO='1',BTS_VERIFY_GPU_STACK='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=240)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'ribbon.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
for v in c.values():
 assert len(v['paths'])==1 and v['paths'][0]['ribbon']['enabled']
 assert v['paths'][0]['version']==5 and len(v['paths'][0]['points'])==3
 assert v['document']['schema']>=58 and len(v['document']['ribbon_images'])==1
 assert v['decimal']=='.' and not v['shader_failures']
 assert len(v['paths'][0]['ribbon']['image']['packed'])>100
assert c['stretched']['hash']!=c['repeated']['hash']
assert c['stretched']['hash']==c['undo-repeat']['hash']
assert c['repeated']['hash']==c['redo-repeat']['hash']
assert c['repeated']['undo_count']==c['stretched']['undo_count']+1
for before,after in [('redo-repeat','aspect'),('aspect','oriented'),('oriented','curved'),('curved','wider'),('opened-packed','edited-after-open')]:assert c[before]['hash']!=c[after]['hash'],(before,after)
assert c['saved']['document']==c['opened-packed']['document']
assert c['saved']['hash']==c['opened-packed']['hash']
assert c['saved']['native_hash']==c['opened-packed']['native_hash']
assert c['edited-after-open']['paths'][0]['ribbon']['orientation']==2
assert not (a.output/'ribbon-source.png').exists()
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',continuous_rgba_surface_ribbon=True,image_file=True,stretch_and_custom_repeat=True,mirror=True,keep_aspect_and_clip=True,orientation_and_flip=True,editable_curve=True,width_edit=True,one_settings_undo=True,redo=True,pooled_scene_linear_image=True,packed_open_without_source=True,editable_after_open=True)
match=re.search(r'BTS_GPU_RIBBON_VERIFIED cases=(\d+).*max_error=([^\s]+)',log)
assert match, 'Ribbon GPU oracle missing'
result.update(gpu_cases=int(match[1]),gpu_max_error=float(match[2]),authoring_cpu_materializations=0,desktop_presentation_checked=False)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
