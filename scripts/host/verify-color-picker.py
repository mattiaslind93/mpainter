# SPDX-License-Identifier: GPL-3.0-or-later
"""Native sRGB authoring / scene-linear storage and one-gesture color history."""
from parity_test_environment import private_display
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import struct

p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
# Preserve the actual sphere/UV geometry, omit the explicit MTL color to test defaults.
mesh=a.output/'unassigned.obj'
mesh.write_text('\n'.join(line for line in (root/'native/tests/fixtures/single_tile.obj').read_text().splitlines() if not line.startswith(('mtllib ','usemtl ')))+'\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_COLOR_PICKER_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
    process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
    try:code=process.wait(timeout=300)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
demo=json.loads((a.output/'color-picker.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']}
def close(actual,expected,tolerance=2e-3):
    assert len(actual)==len(expected) and max(abs(a-b) for a,b in zip(actual,expected))<tolerance,(actual,expected)
close(captures['created']['brush_color'],[0,0,0,1]);close(captures['created']['base_color'],[1,1,1,1])
# Independent AP1 red-primary reference, including sRGB transfer and D65/D60 adaptation.
red=[.6131,.0702,.0206,1]
for stage in ['wheel-red','hex-red']:close(captures[stage]['brush_color'],red)
assert captures['undo-brush-color']['brush_color']!=captures['hex-red']['brush_color']
close(captures['redo-brush-color']['brush_color'],red)
assert captures['undo-brush-color']['brush_size']==captures['redo-brush-color']['brush_size']==12
assert captures['tablet-square']['brush_color']!=captures['hex-red']['brush_color']
close(captures['fill-white']['layer_color'],[1,1,1,1])
drag=captures['fill-drag'];assert drag['undo_count']==demo['before_drag_undo']+1
close(drag['layer_color'],drag['picker_color']);close(captures['undo-drag']['layer_color'],red)
def srgb8_channel(value):
    encoded=12.92*value if value<=.0031308 else 1.055*value**(1/2.4)-.055
    quantized=int(encoded*255+.5)/255
    return quantized/12.92 if quantized<=.04045 else ((quantized+.055)/1.055)**2.4
close(captures['picked-albedo']['brush_color'],[srgb8_channel(v) for v in captures['hex-red']['brush_color'][:3]]+[1],3e-5)
assert captures['before-pick']['brush_color']!=captures['picked-albedo']['brush_color']
assert abs(captures['hdr']['brush_color'][0]-2.5)<1e-6
close(captures['hdr']['brush_color'],captures['hdr-reopened']['brush_color'],1e-6)
assert demo['point_decimal'] and len(demo['recent'])>=8 and demo['drag_moves']>=100
assert demo['max_ping_ms']<100,demo['max_ping_ms']
assert len(demo['live_previews'])==100
for preview in demo['live_previews'][1:]:
    assert preview['native_color_gpu_resident'];close(preview['native_color'],[struct.unpack('e',struct.pack('e',v))[0] for v in preview['native_color_expected']],1e-5)
recent=[[float(c) for c in item.split(',')] for item in demo['recent']]
assert any(max(abs(a-b) for a,b in zip(color,red))<2e-3 for color in recent)
assert all(c['decimal']=='.' and not c['shader_failures'] for c in captures.values())
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',new_brush_black_base_fill_white=True,srgb_red_acescg=captures['hex-red']['brush_color'],mouse_and_pen_wheel=True,albedo_eyedropper=True,recent_count=len(recent),hdr_preserved=True,drag_changes=demo['drag_moves'],one_undo=True,native_live_rgb_verified=True,brush_color_undo_redo_preserves_size=True,max_ping_ms=demo['max_ping_ms'],point_decimal=True,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
