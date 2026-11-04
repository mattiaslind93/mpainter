"""Native unlit GPU material picker, channel precision and async packet."""
import argparse,json,os,re,subprocess
from pathlib import Path
from types import SimpleNamespace
from PIL import Image,ImageStat,ImageChops
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(a.output),'--picker-demo','--recovery-dir',str(a.output/'recovery')]
if not a.existing:
 with (a.output/'run.log').open('w') as log:
  completed=subprocess.run(command,env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
 assert completed.returncode==0
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_MATERIAL_PICK_FAILED',log)
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']};stages=list(captures)
assert stages==['initial','enabled','bare','enabled-stripe','stripe','partial','enabled3d','sampled3d','rough-disabled','masked-channel','eraser','eraser-sampled','empty','cancelled']
first=captures['initial'];errors={}
for stage in ['enabled','enabled-stripe','enabled3d']:assert captures[stage]['picker_active']
for stage in ['bare','stripe','partial','sampled3d','masked-channel','eraser-sampled','empty','cancelled']:assert not captures[stage]['picker_active']
for stage,roughness in [('bare',.2),('stripe',1),('partial',.6)]:assert abs(captures[stage]['picker_brush'][5]-roughness)<1e-6,(stage,captures[stage]['picker_brush'])
for stage in ['bare','stripe','partial','sampled3d','eraser-sampled']:
    channels=captures[stage]['document']['texture_sets'][0]['channels'];components=[0,0,0,0,1,2,3,3,3,4]
    assert max(abs(a-b) for i,(a,b) in enumerate(zip(captures[stage]['picker_brush'],captures[stage]['picker_expected'])) if channels[components[i]])<3e-5,stage
# Normal is no longer a default document channel. A pick must retain the brush's
# existing normal, rather than replace it with the CPU composite's neutral value.
assert not first['document']['texture_sets'][0]['channels'][3]
for stage in ['bare','stripe','partial','sampled3d','masked-channel']:
    assert captures[stage]['picker_brush'][6:9]==first['picker_brush'][6:9],stage
assert abs(captures['masked-channel']['picker_brush'][5]-.07)<1e-6
assert captures['eraser']['workspace']['eraser_active'] and captures['eraser-sampled']['workspace']['eraser_active']
assert captures['empty']['picker_brush']==captures['cancelled']['picker_brush']==captures['eraser-sampled']['picker_brush']
assert captures['bare']['picker_brush']!=first['picker_brush']
for stage,c in captures.items():
    assert c['document']==first['document'],stage
    assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
    image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');assert image.width>100 and image.height>100
    assert max(ImageStat.Stat(image).var)>1
    # Offscreen RTX runs have no desktop-compositor window capture.
    window_path=a.output/f'{stage}-window.png'
    if window_path.exists():
        window=Image.open(window_path).convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    else:assert os.environ.get('BTS_HEADLESS_WORKSPACE')=='1'
result['picker_demo']={'pid':process.pid,'stages':stages,'known_value_transfer_uv_and_native_3d':True,'partial_stroke_samples_composite':True,'brush_disabled_channel_preserved':True,'returns_to_paint_or_eraser_without_pixel_edits':True,'background_and_escape_keep_values':True,'presentation_errors':errors,'desktop_presentation_checked':bool(errors)}
backend=re.search(r'BTS_GPU_MATERIAL_PICK_VERIFIED cases=(\d+).*max_error=(\S+)',log);assert backend and int(backend[1])>=83
samples=re.findall(r'BTS_GPU_MATERIAL_PICK ticket=(\d+) packet_bytes=96 cpu_composition=0',log);assert len(samples)==6,samples
result['gpu']={'backend_cases':int(backend[1]),'maximum_error':float(backend[2]),'native_samples':len(samples),'packet_bytes':96,'cpu_composition':False,'precision':'Project channel precision, unlit scene-linear authoring data; no baked AO or bump normal.'}
(a.output/'material-picker-gpu.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
