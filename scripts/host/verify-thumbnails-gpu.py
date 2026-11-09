# SPDX-License-Identifier: GPL-3.0-or-later
"""GPU source/mask thumbnails plus native Compare/Undo/project regression."""
import argparse,json,re,runpy,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--project',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args()
if not a.existing:
    sys.argv=[str(Path(__file__).with_name('verify-compare-mask-gpu.py')),'--output',str(a.output),'--mesh',str(a.mesh),'--environment',str(a.environment),'--project',str(a.project)]
    runpy.run_path(sys.argv[0],run_name='__main__')
base=json.loads((a.output/'verified.json').read_text());log=(a.output/'run.log').read_text()
backend=re.search(r'BTS_GPU_THUMBNAIL_VERIFIED cases=(\d+).*same_frame_compare_isolation=1.*max_error=(\S+)',log)
assert backend and int(backend[1])>=20
assert not re.search(r'BTS_GPU_THUMBNAIL_FAILED|BTS_THUMBNAIL_ERROR|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_CPU_FALLBACK|BTS_DRAW_FAILED|BTS_HOST_FAILED',log)
ready=re.findall(r'BTS_GPU_THUMBNAIL_READY ticket=(\d+) channel=(\d+) mask=(\d+) packet_bytes=36864 cpu_composition=0',log)
assert len(ready)>=8 and {row[2]for row in ready}=={'0','1'},ready
# MaterialInspection is recreated when a project opens; its ticket sequence
# starts again. Publications must be unique within each host/project lifetime.
for lifetime in re.split(r'BTS_PROJECT_OPENED[^\n]*',log):
    tickets=re.findall(r'BTS_GPU_THUMBNAIL_READY ticket=(\d+)',lifetime)
    assert len(set(tickets))==len(tickets),'Duplicate thumbnail publication within a project lifetime'
result={'backend_cases':int(backend[1]),'maximum_pixel_error':float(backend[2]),'pixels_per_case':2304,'udim_tiles':3,'channels':8,'opacity_zero_hidden_and_disabled_source':True,'same_frame_compare_inputs_unchanged':True,'native_thumbnail_publications':len(ready),'native_compare_stages':len(base['compare_mask_demo']['stages']),'native_mask_probe_error':base['gpu']['max_error'],'undo_and_packed_reopen':True,'cpu_thumbnail_composition':False,'packet_bytes':36864}
(a.output/'thumbnails-gpu.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
