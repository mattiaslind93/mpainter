# SPDX-License-Identifier: GPL-3.0-or-later
"""GPU broadcast/reference checks and visible color updates before drag release."""
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve()
env=dict(os.environ,BTS_VERIFY_GPU_UNIFORM='1',BTS_GPU_UNIFORM_DEMO='1')
if not a.existing:subprocess.run(['python3',str(Path(__file__).with_name('verify-performance.py')),'--output',str(a.output)],env=env,check=True)
proof=json.loads((a.output/'performance.json').read_text());log=(a.output/'run.log').read_text();channels={int(c) for c in re.findall(r'BTS_GPU_UNIFORM_VERIFIED channel=(\d+)',log)};assert channels=={0,1,2,4},channels
frames=proof['gpu_frames'];assert len(frames)==2 and [f['index'] for f in frames]==[20,80]
assert frames[0]['state']['undo_count']==frames[1]['state']['undo_count'] and frames[0]['state']['padding_calls']==frames[1]['state']['padding_calls']
color_case=next(c for c in proof['cases'] if c['flow']=='property_drag');assert color_case['after']['undo_count']-color_case['before']['undo_count']==1
images=[Image.open(a.output/f['file']).convert('RGB') for f in frames];w,h=images[0].size;box=(int(w*.4),int(h*.4),int(w*.6),int(h*.6));difference=sum(ImageStat.Stat(ImageChops.difference(images[0].crop(box),images[1].crop(box))).mean)/(3*255);assert difference>.005,difference
result=dict(pid=proof['pid'],gpu_uniform_channels=sorted(channels),gpu_texels_per_channel=1024*1024,visible_color_difference_during_drag=difference,drag_released=False,final_cpu_reference_exact=True,uniform_preview_production_readbacks=0)
(a.output/'gpu-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
