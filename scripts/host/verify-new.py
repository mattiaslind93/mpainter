# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded native New-project proof; run inside bts-build."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
from PIL import Image, ImageChops, ImageStat, ImageFilter

def export_channel_suffix(name):
    return {'Base_Color':'color','Height':'bump','Bump':'bump'}.get(name,name.lower())

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mesh', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
p.add_argument('--sets', action='store_true')
p.add_argument('--channels', action='store_true')
p.add_argument('--color', action='store_true')
p.add_argument('--no-fill', action='store_true')
p.add_argument('--paint', action='store_true')
p.add_argument('--flow', action='store_true')
p.add_argument('--opacity', action='store_true')
p.add_argument('--spacing', action='store_true')
p.add_argument('--smart-material',type=Path)
p.add_argument('--mesh-maps',action='store_true')
p.add_argument('--selective-bake',action='store_true')
p.add_argument('--bake-quality',action='store_true')
p.add_argument('--cage',type=Path)
p.add_argument('--bake-match',type=Path)
p.add_argument('--utility-maps',type=Path)
p.add_argument('--normal-mix',type=Path)
p.add_argument('--height',type=Path)
p.add_argument('--recover-channel',type=Path)
p.add_argument('--ao',type=Path)
p.add_argument('--export',dest='texture_export',type=Path)
p.add_argument('--export-naming',type=Path)
p.add_argument('--export-size',type=Path)
p.add_argument('--export-packing',type=Path)
p.add_argument('--export-padding',type=Path)
p.add_argument('--export-settings',type=Path)
p.add_argument('--reimport',type=Path)
p.add_argument('--uv-reproject',type=Path)
p.add_argument('--reassign',type=Path)
p.add_argument('--project-template',type=Path)
p.add_argument('--normal-format',type=Path)
p.add_argument('--fragment-tangent',type=Path)
p.add_argument('--levels',type=Path)
p.add_argument('--compare-mask',type=Path)
p.add_argument('--paint-effect',type=Path)
p.add_argument('--filter-effect',type=Path)
p.add_argument('--fill-effect',type=Path)
p.add_argument('--anchor-reference',type=Path)
p.add_argument('--anchor-validity',type=Path)
p.add_argument('--anchor-navigation',type=Path)
p.add_argument('--geometry-mask',action='store_true')
p.add_argument('--tile-mask',action='store_true')
p.add_argument('--hide-excluded',type=Path)
p.add_argument('--instances',action='store_true')
p.add_argument('--paint-instance',type=Path)
p.add_argument('--instance-cycles',type=Path)
p.add_argument('--jitter',action='store_true')
p.add_argument('--stencil',action='store_true')
p.add_argument('--follow-path',action='store_true')
p.add_argument('--material-projection',action='store_true')
p.add_argument('--smudge',action='store_true')
p.add_argument('--clone',action='store_true')
p.add_argument('--quick-mask',action='store_true')
p.add_argument('--dynamic-stroke',action='store_true')
p.add_argument('--surface-path',action='store_true')
p.add_argument('--editable-path',type=Path)
p.add_argument('--visibility-shape',action='store_true')
p.add_argument('--mesh-generator',type=Path)
p.add_argument('--mask-effects',type=Path)
p.add_argument('--mask-replace',type=Path)
p.add_argument('--smart-mask',type=Path)
p.add_argument('--smart-mask-library',type=Path)
p.add_argument('--mask-copy',type=Path)
p.add_argument('--color-selection',type=Path)
p.add_argument('--passthrough',type=Path)
p.add_argument('--normal-vectors',type=Path)
p.add_argument('--blend-families',type=Path)
p.add_argument('--tile-resolution',type=Path)
p.add_argument('--custom-channel',type=Path)
p.add_argument('--precision',type=Path)
p.add_argument('--rectangular',type=Path)
p.add_argument('--dynamic-resolution',type=Path)
p.add_argument('--physical',action='store_true')
p.add_argument('--usd',type=Path)
p.add_argument('--auto-unwrap',type=Path)
p.add_argument('--cameras',type=Path)
p.add_argument('--template-load',type=Path)
p.add_argument('--replacement',type=Path)
p.add_argument('--selective-udim',action='store_true')
p.add_argument('--asset-categories',type=Path)
p.add_argument('--asset-search',type=Path)
p.add_argument('--resources',type=Path)
p.add_argument('--triplanar',action='store_true')
p.add_argument('--fill-uv',action='store_true')
p.add_argument('--polygon',action='store_true')
p.add_argument('--picker',action='store_true')
p.add_argument('--lazy',action='store_true')
p.add_argument('--straight',action='store_true')
p.add_argument('--symmetry',action='store_true')
p.add_argument('--projection',action='store_true')
p.add_argument('--padding',action='store_true')
p.add_argument('--alpha',type=Path)
p.add_argument('--eraser',action='store_true')
p.add_argument('--invert-mask',action='store_true')
p.add_argument('--isolate-mask',action='store_true')
p.add_argument('--bitmap-mask',type=Path)
p.add_argument('--masks', action='store_true')
p.add_argument('--blend', action='store_true')
p.add_argument('--replace', action='store_true')
p.add_argument('--selection', action='store_true')
p.add_argument('--folders', action='store_true')
p.add_argument('--layer-drag', action='store_true')
p.add_argument('--layer-opacity', action='store_true')
p.add_argument('--layer-context', action='store_true')
p.add_argument('--alignment', action='store_true')
p.add_argument('--resource', type=Path)
p.add_argument('--appearance', action='store_true')
p.add_argument('--environment-demo', action='store_true')
p.add_argument('--udim', action='store_true')
p.add_argument('--surface-volume',action='store_true')
p.add_argument('--surface', action='store_true')
p.add_argument('--pressure', action='store_true')
p.add_argument('--project-roundtrip', action='store_true')
p.add_argument('--inspection', action='store_true')
p.add_argument('--fill', action='store_true')
p.add_argument('--timeout', type=float, default=180)
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
root = repo.parent
a.output.mkdir(parents=True, exist_ok=True)
source_colors = {'CubeSurface':[.1,.65,.25], 'SphereSurface':[.8,.1,.2]}
color_expected = None
if a.color:
    source = a.mesh.read_text()
    assert 'SphereSurface' in source and 'CubeSurface' in source
    mesh = a.output/'color.obj'
    mesh.write_text(re.sub(r'^mtllib .*$', 'mtllib color.mtl', source, flags=re.MULTILINE))
    (a.output/'color.mtl').write_text(''.join(f'newmtl {name}\nKd {v[0]} {v[1]} {v[2]}\nillum 2\n' for name,v in source_colors.items()))
    a.mesh = mesh
    ocio_script = a.output/'ocio-reference.py'
    ocio_script.write_text("""import json,sys
import PyOpenColorIO as ocio
config=ocio.Config.CreateFromFile(sys.argv[1])
processor=config.getProcessor('Linear Rec.709','ACEScg').getDefaultCPUProcessor()
colors=json.loads(sys.argv[2])
print(json.dumps({k:processor.applyRGB(v) for k,v in colors.items()}))
""")
    config = root/'blender/release/datafiles/colormanagement/config.ocio'
    color_expected = json.loads(subprocess.check_output([
        str(root/'blender/lib/linux_x64/python/bin/python3.13'),str(ocio_script),str(config),json.dumps(source_colors)],
        env={**os.environ,'LD_LIBRARY_PATH':str(root/'build/baseline/bin/lib')},text=True))
    (a.output/'ocio-reference.json').write_text(json.dumps({'config_sha256':hashlib.sha256(config.read_bytes()).hexdigest(),
        'source':'Linear Rec.709', 'destination':'ACEScg','inputs':source_colors,'expected':color_expected},indent=2))
command = ['bash', str(repo/'scripts/host/run-host.sh'), '--workspace', '--project',
           str(root/'build/baseline-demo/host-fixture.blend'), '--environment', str(a.environment),
           '--new-mesh', str(a.mesh), '--new-demo-dir', str(a.output)]
if a.sets: command.append('--texture-sets-demo')
if a.channels: command.append('--channels-demo')
if a.color: command.append('--color-demo')
if a.fill: command.append('--fill-demo')
if a.inspection: command.append('--inspection-demo')
resource_source=None
if a.resources or a.asset_search or a.asset_categories:
    resource_source=a.output/'SphereSurface_Roughness.png'
    resource_source.write_bytes((a.resources or a.asset_search or a.asset_categories).read_bytes())
    command.extend(['--resources-demo' if a.resources else '--asset-search-demo' if a.asset_search else '--asset-categories-demo',str(resource_source),'--library-dir',str(a.output/'user-shelf')])
utility_source=None
if a.normal_mix: command.extend(['--normal-mix-demo',str(a.normal_mix)])
if a.height: command.extend(['--height-demo',str(a.height)])
if a.editable_path:command.extend(['--editable-path-demo',str(a.editable_path),'--recovery-dir',str(a.output/'recovery')])
if a.material_projection or a.smudge or a.clone or a.quick_mask or a.dynamic_stroke or a.surface_path:
    fixture=a.output/'material-projection.obj'
    fixture.write_text('mtllib material-projection.mtl\no ProjectionPlane\nusemtl A_ProjectionSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\no OtherPlane\nusemtl Z_OtherSurface\nv 1.1 -.2 0\nv 1.5 -.2 0\nv 1.5 .2 0\nv 1.1 .2 0\nf 5/1 6/2 7/3 8/4\n')
    fixture.with_suffix('.mtl').write_text('newmtl A_ProjectionSurface\nKd .5 .5 .5\nnewmtl Z_OtherSurface\nKd .3 .3 .3\n')
    command.extend(['--surface-path-demo' if a.surface_path else '--dynamic-stroke-demo' if a.dynamic_stroke else '--quick-mask-demo' if a.quick_mask else '--clone-demo' if a.clone else '--smudge-demo' if a.smudge else '--material-projection-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')])
if a.follow_path:
    fixture=a.output/'follow-path.obj'
    fixture.write_text('mtllib follow-path.mtl\no BrushPlane\nusemtl BrushSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
    fixture.with_suffix('.mtl').write_text('newmtl BrushSurface\nKd .5 .5 .5\n')
    command.extend(['--follow-path-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')])
if a.stencil:
    fixture=a.output/'brush-stencil.obj'
    fixture.write_text('mtllib brush-stencil.mtl\no BrushPlane\nusemtl BrushSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
    fixture.with_suffix('.mtl').write_text('newmtl BrushSurface\nKd .5 .5 .5\n')
    command.extend(['--stencil-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')])
if a.jitter:
    fixture=a.output/'brush-jitter.obj'
    fixture.write_text('mtllib brush-jitter.mtl\no BrushPlane\nusemtl BrushSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
    fixture.with_suffix('.mtl').write_text('newmtl BrushSurface\nKd .5 .5 .5\n')
    command.extend(['--jitter-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')])
if a.instance_cycles: command.extend(['--instance-cycle-demo',str(a.instance_cycles),'--recovery-dir',str(a.output/'recovery')])
if a.paint_instance: command.extend(['--paint-instance-demo',str(a.paint_instance),'--recovery-dir',str(a.output/'recovery')])
if a.instances: command.extend(['--instance-demo','--recovery-dir',str(a.output/'recovery')])
if a.hide_excluded: command.extend(['--hide-excluded-demo',str(a.hide_excluded),'--recovery-dir',str(a.output/'recovery')])
if a.visibility_shape: command.append('--visibility-shape-demo')
if a.geometry_mask or a.tile_mask:
    fixture=a.output/'geometry-mask.obj'
    fixture.write_text("mtllib geometry-mask.mtl\no LeftPart\nusemtl SharedSurface\nv -1.3 -.5 0\nv -.3 -.5 0\nv -.3 .5 0\nv -1.3 .5 0\nvt .05 .1\nvt .45 .1\nvt .45 .9\nvt .05 .9\nf 1/1 2/2 3/3 4/4\no RightPart\nusemtl SharedSurface\nv .3 -.5 0\nv 1.3 -.5 0\nv 1.3 .5 0\nv .3 .5 0\nvt .55 .1\nvt .95 .1\nvt .95 .9\nvt .55 .9\nf 5/5 6/6 7/7 8/8\n")
    fixture.with_suffix('.mtl').write_text('newmtl SharedSurface\nKd .5 .5 .5\n')
    if a.tile_mask:
        fixture.write_text(fixture.read_text().replace('vt .55 .1','vt 1.05 .1').replace('vt .95 .1','vt 1.95 .1').replace('vt .95 .9','vt 1.95 .9').replace('vt .55 .9','vt 1.05 .9'))
    command.extend(['--tile-mask-demo' if a.tile_mask else '--geometry-mask-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')])
if a.anchor_navigation: command.extend(['--anchor-navigation-demo',str(a.anchor_navigation),'--recovery-dir',str(a.output/'recovery')])
if a.anchor_validity: command.extend(['--anchor-validity-demo',str(a.anchor_validity),'--recovery-dir',str(a.output/'recovery')])
if a.anchor_reference: command.extend(['--anchor-reference-demo',str(a.anchor_reference),'--recovery-dir',str(a.output/'recovery')])
if a.fill_effect: command.extend(['--fill-effect-demo',str(a.fill_effect),'--recovery-dir',str(a.output/'recovery')])
if a.filter_effect: command.extend(['--filter-effect-demo',str(a.filter_effect),'--recovery-dir',str(a.output/'recovery')])
if a.paint_effect: command.extend(['--paint-effect-demo',str(a.paint_effect),'--recovery-dir',str(a.output/'recovery')])
if a.compare_mask: command.extend(['--compare-mask-demo',str(a.compare_mask),'--recovery-dir',str(a.output/'recovery')])
if a.levels: command.extend(['--levels-demo',str(a.levels),'--recovery-dir',str(a.output/'recovery')])
if a.mesh_generator:
    fixtures=a.output/'generator-inputs';fixtures.mkdir(exist_ok=True)
    for family in ['position','curvature']:
        for flipped in [False,True]:
            pixels=[]
            for y in range(128):
                for x in range(128):
                    if family=='position':
                        value=round(255*x/127);pixels.append((51,204,255-value if flipped else value))
                    else:
                        value=32 if x<64 else 224;value=256-value if flipped else value;pixels.append((value,value,value))
            im=Image.new('RGB',(128,128));im.putdata(pixels);im.save(fixtures/f"{family}-{'flipped' if flipped else 'forward'}.png")
    Image.new('RGB',(31,64),(0,0,0)).save(fixtures/'invalid.png')
    command.extend(['--mesh-generator-demo',str(a.mesh_generator),'--generator-fixtures',str(fixtures),'--recovery-dir',str(a.output/'recovery')])
if a.mask_effects: command.extend(['--mask-effects-demo',str(a.mask_effects),'--recovery-dir',str(a.output/'recovery')])
if a.mask_replace: command.extend(['--mask-replace-demo',str(a.mask_replace),'--recovery-dir',str(a.output/'recovery')])
if a.smart_mask: command.extend(['--smart-mask-demo',str(a.smart_mask),'--recovery-dir',str(a.output/'recovery'),'--library-dir',str(a.smart_mask_library or a.output/'mask-library')])
if a.mask_copy: command.extend(['--mask-copy-demo',str(a.mask_copy),'--recovery-dir',str(a.output/'recovery')])
if a.color_selection: command.extend(['--color-selection-demo',str(a.color_selection),'--recovery-dir',str(a.output/'recovery')])
if a.passthrough: command.extend(['--passthrough-demo',str(a.passthrough),'--recovery-dir',str(a.output/'recovery')])
if a.normal_vectors: command.extend(['--normal-vectors-demo',str(a.normal_vectors),'--recovery-dir',str(a.output/'recovery')])
if a.blend_families: command.extend(['--blend-families-demo',str(a.blend_families),'--recovery-dir',str(a.output/'recovery')])
if a.tile_resolution: command.extend(['--tile-resolution-demo',str(a.tile_resolution),'--recovery-dir',str(a.output/'recovery')])
if a.custom_channel: command.extend(['--custom-channel-demo',str(a.custom_channel),'--recovery-dir',str(a.output/'recovery')])
if a.precision: command.extend(['--precision-demo',str(a.precision),'--recovery-dir',str(a.output/'recovery')])
if a.rectangular: command.extend(['--rectangular-demo',str(a.rectangular),'--recovery-dir',str(a.output/'recovery')])
if a.dynamic_resolution: command.extend(['--dynamic-resolution-demo',str(a.dynamic_resolution),'--recovery-dir',str(a.output/'recovery')])
if a.physical:
    physical_source=a.output/'meters.usda'
    def unit_scene(meters,size):
        return '#usda 1.0\n(\n metersPerUnit = '+str(meters)+'\n upAxis = "Y"\n)\ndef Mesh "Surface" {\n uniform token subdivisionScheme = "none"\n int[] faceVertexCounts = [4]\n int[] faceVertexIndices = [0,1,2,3]\n point3f[] points = [(-'+str(size)+',-'+str(size)+',0),('+str(size)+',-'+str(size)+',0),('+str(size)+','+str(size)+',0),(-'+str(size)+','+str(size)+',0)]\n texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (interpolation = "faceVarying")\n}\n'
    physical_source.write_text(unit_scene(1,.1));(a.output/'centimeters.usda').write_text(unit_scene(.01,10))
    (a.output/'plane.obj').write_text('v -0.1 -0.1 0\nv 0.1 -0.1 0\nv 0.1 0.1 0\nv -0.1 0.1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
    command.extend(['--physical-scale-demo',str(physical_source),'--recovery-dir',str(a.output/'recovery')])
if a.usd:
    usd_source=a.output/'usd-source.usda';original=a.usd.read_text();usd_source.write_text(original)
    without_uv,count=re.subn(r'(?m)^[ \t]*texCoord2f\[\] primvars:st = [^\n]*\(\n[ \t]*interpolation = "faceVarying"\n[ \t]*\)\n','',original)
    assert count==3,'USD contract fixture must contain three authored UV maps'
    Path(str(usd_source)+'.no-uv.usda').write_text(without_uv)
    command.extend(['--usd-scene-demo',str(usd_source),'--recovery-dir',str(a.output/'recovery')])
if a.auto_unwrap: command.extend(['--auto-unwrap-demo',str(a.auto_unwrap),'--recovery-dir',str(a.output/'recovery')])
if a.cameras: command.extend(['--camera-import-demo',str(a.cameras),'--recovery-dir',str(a.output/'recovery')])
if a.fragment_tangent: command.extend(['--fragment-tangent-demo',str(a.fragment_tangent),'--recovery-dir',str(a.output/'recovery')])
if a.normal_format: command.extend(['--normal-format-demo',str(a.normal_format),'--recovery-dir',str(a.output/'recovery')])
if a.project_template: command.extend(['--template-demo',str(a.project_template),'--recovery-dir',str(a.output/'recovery')])
if a.template_load: command.extend(['--template-load-demo',str(a.template_load),'--recovery-dir',str(a.output/'recovery')])
if a.reassign: command.extend(['--reassign-demo',str(a.reassign),'--replacement-mesh',str(a.replacement),'--recovery-dir',str(a.output/'recovery')])
if a.uv_reproject: command.extend(['--uv-reproject-demo',str(a.uv_reproject),'--replacement-mesh',str(a.replacement),'--recovery-dir',str(a.output/'recovery')])
if a.reimport:
    assert a.replacement
    wrong=a.output/'wrong';wrong.mkdir(exist_ok=True);text=a.replacement.read_text();assert 'usemtl CubeSurface' in text
    (wrong/'replacement.obj').write_text('\n'.join(('vt '+str(float(line.split()[1])+1)+' '+line.split()[2]) if line.startswith('vt ') else line for line in text.splitlines()));(wrong/'two_materials.mtl').write_bytes(a.replacement.with_name('two_materials.mtl').read_bytes())
    command.extend(['--mesh-reimport-demo',str(a.reimport),'--replacement-mesh',str(a.replacement),'--recovery-dir',str(a.output/'recovery')])
if a.export_settings: command.extend(['--export-settings-demo',str(a.export_settings)])
if a.export_padding: command.extend(['--export-padding-demo',str(a.export_padding)])
if a.export_packing: command.extend(['--export-packing-demo',str(a.export_packing)])
if a.export_size: command.extend(['--export-size-demo',str(a.export_size)])
if a.export_naming: command.extend(['--export-naming-demo',str(a.export_naming)])
if a.texture_export: command.extend(['--export-demo',str(a.texture_export)])
if a.recover_channel: command.extend(['--recover-channel-demo',str(a.recover_channel),'--recovery-dir',str(a.output/'recovery')])
if a.ao: command.extend(['--ao-demo',str(a.ao)])
if a.utility_maps:
    utility_source=a.output/'import-sources';utility_source.mkdir(exist_ok=True)
    for name in ['CubeSurface_normal.png','CubeSurface_normal_base.png','SphereSurface_ambient_occlusion.png','WrongSet_curvature.png']:(utility_source/name).write_bytes((a.utility_maps/name).read_bytes())
    (a.output/'bad').mkdir(exist_ok=True);(a.output/'bad/CubeSurface_curvature.png').write_bytes(b'not a supported image')
    command.extend(['--utility-import-demo',str(utility_source)])
if a.smart_material: command.extend(['--smart-material-demo',str(a.smart_material)])
if a.mesh_maps: command.append('--mesh-maps-demo')
if a.selective_bake: command.append('--selective-bake-demo')
if a.bake_quality: command.append('--bake-quality-demo')
if a.cage: command.extend(['--cage-demo',str(a.cage)])
if a.bake_match: command.extend(['--bake-match-demo',str(a.bake_match)])
if a.selective_udim: command.append('--selective-bake-udim')
if a.triplanar: command.append('--triplanar-demo')
if a.fill_uv: command.append('--fill-uv-demo')
if a.polygon: command.append('--polygon-demo')
if a.picker: command.append('--picker-demo')
if a.lazy: command.append('--lazy-demo')
if a.straight: command.append('--straight-demo')
if a.symmetry: command.append('--symmetry-demo')
if a.projection: command.append('--projection-demo')
if a.padding: command.append('--padding-demo')
if a.paint: command.append('--paint-demo')
if a.eraser: command.append('--eraser-demo')
if a.alpha: command.extend(['--alpha-demo',str(a.alpha)])
if a.flow: command.append('--flow-demo')
if a.opacity: command.append('--opacity-demo')
if a.spacing: command.append('--spacing-demo')
if a.masks: command.append('--mask-demo')
if a.invert_mask: command.append('--invert-mask-demo')
if a.isolate_mask: command.append('--isolate-mask-demo')
if a.bitmap_mask: command.extend(['--bitmap-mask-demo',str(a.bitmap_mask)])
if a.blend: command.append('--blend-demo')
if a.replace: command.append('--replace-demo')
if a.selection: command.append('--selection-demo')
if a.folders: command.append('--folder-demo')
if a.layer_drag: command.append('--layer-drag-demo')
if a.layer_opacity: command.append('--layer-opacity-demo')
if a.layer_context: command.append('--layer-context-demo')
if a.alignment: command.append('--alignment-demo')
if a.resource: command.extend(['--resource-demo','--alternative-environment',str(a.resource)])
if a.appearance: command.append('--appearance-demo')
if a.environment_demo: command.append('--environment-demo')
if a.udim: command.append('--udim-demo')
if a.surface: command.append('--surface-demo')
if a.surface_volume: command.append('--surface-volume-demo')
if a.pressure: command.append('--pressure-demo')
if a.project_roundtrip: command.append('--project-roundtrip')
if a.no_fill: command.append('--paint-no-fill')
children = set()
started = time.monotonic()
with (a.output/'run.log').open('w') as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                               env={**os.environ, 'BTS_ROOT': str(root)})
    while process.poll() is None:
        try:
            if Path(f'/proc/{process.pid}/exe').resolve(strict=True).name == 'bts-host':
                for task in Path(f'/proc/{process.pid}/task').glob('*/children'):
                    children.update(task.read_text().split())
        except FileNotFoundError:
            pass
        if utility_source is not None and 'BTS_PROJECT_SAVED' in (a.output/'run.log').read_text():
            for source in utility_source.glob('*.png'):source.unlink()
        if resource_source is not None and resource_source.exists() and 'stage=saved' in (a.output/'run.log').read_text():
            resource_source.unlink() # Only the verification-owned copy, never the supplied fixture.
        if time.monotonic()-started > a.timeout:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise SystemExit('New demo timed out')
        time.sleep(.1)
assert process.returncode == 0, f'Native New exited {process.returncode}; see {a.output}/run.log'
assert not children, children
log = (a.output/'run.log').read_text()
assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
result = json.loads((a.output/'new.json').read_text())
assert result['pid'] == process.pid
assert result['invalid_import_preserved']
for marker in ['BTS_SCENE_READY', 'BTS_GPU_READY', 'BTS_FRAME', 'BTS_NEW_PROJECT']:
    lines = [line for line in log.splitlines() if line.startswith(marker)]
    assert lines and all(int(re.search(r'pid=(\d+)', line)[1]) == process.pid for line in lines)
doc = result['document']
assert doc['resolution'] == 1024 and doc['normal_format'] == 'OpenGL'
assert doc['uv_workflow'] == ('preserve_tiles' if a.udim or a.selective_udim else 'single_tile')
assert doc['texture_sets'] and len(doc['texture_sets']) == len(result['native']['materials'])
assert set(s['name'] for s in doc['texture_sets']) == set(result['workspace']['texture_sets'])
assert 'Copper' not in result['workspace']['texture_sets'], 'Previous project material survived New'
assert result['native']['mesh_objects'] > 0 and result['native']['uv_triangles'] > 0
assert result['view']['last_framed_bounds']['mesh_objects'] == result['native']['mesh_objects']
for texture_set in doc['texture_sets']:
    assert texture_set['channels'] == [True]*5
    assert len(texture_set['layers']) == 1
    layer = texture_set['layers'][0]
    assert layer['name'] == 'Layer 1' and layer['kind'] == 'paint' and layer['coverage'] == 'empty'
profile = result['profile']
expected = json.loads((repo/'verification/environment/passed-39124704187/studiolight-native/new/ready.json').read_text())['state']['environment']
# Product override (Mattias 2026-10-19): the negatively mapped RNA flag is false for view-follow lighting.
expected['shading']['use_studiolight_view_rotation'] = False
for key in ['shading', 'eevee', 'ray_tracing_options', 'display']:
    assert profile[key] == expected[key], key
assert profile['working_space'] == 'ACEScg'
assert profile['environment'] == 'TT_environmentLights_v001.hdr'
viewport = Image.open(a.output/'new-viewport.png').convert('RGB')
window = Image.open(a.output/'new-window.png').convert('RGB')
x,y,w,h = result['viewport_rect']
assert max(ImageStat.Stat(viewport).stddev) > 8
error = sum(ImageStat.Stat(ImageChops.difference(viewport, window.crop((x,y,x+w,y+h)))).mean)/(3*255)
assert error < .02
result.update(presentation_error=error, clean_exit=True, children=list(children))
if a.sets:
    demo = json.loads((a.output/'texture-sets.json').read_text())
    assert demo['pid'] == process.pid
    captures = demo['captures']
    assert len(captures) == 5
    expected_selection = ['CubeSurface', 'CubeSurface', 'SphereSurface', 'SphereSurface', 'CubeSurface']
    errors = {}
    images = {}
    for capture, name in zip(captures, expected_selection):
        assert capture['workspace']['texture_sets'] == ['CubeSurface', 'SphereSurface']
        assert capture['workspace']['selected_material_name'] == name
        assert capture['native']['mesh_objects'] == 2
        assert capture['view']['uv_triangles'] > 0
        assert capture['view']['selected_material_session_id'] == capture['workspace']['selected_material_session_id']
        texture_set = next(t for t in capture['document']['texture_sets'] if t['name'] == name)
        assert capture['workspace']['selected_layer_id'].strip('{}') == texture_set['selected_layer']
        for current_set, initial_set in zip(capture['document']['texture_sets'],captures[0]['document']['texture_sets']):
            assert current_set['id']==initial_set['id'] and current_set['channels']==initial_set['channels']
            assert current_set['layers'][0]==initial_set['layers'][0]
            assert [l['id'] for l in current_set['layers']]==[l['id'] for l in initial_set['layers']]
            assert current_set['layers'][-1]['kind']=='fill'
        stage = capture['stage']
        image = Image.open(a.output/f'{stage}-viewport.png').convert('RGB')
        images[stage] = image
        window = Image.open(a.output/f'{stage}-window.png').convert('RGB')
        x,y,w,h = capture['viewport_rect']
        errors[stage] = sum(ImageStat.Stat(ImageChops.difference(image, window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage] < .02
    roughness = [{m['name']:m['roughness'] for m in c['native']['materials']} for c in captures]
    assert roughness[1]['CubeSurface'] == .25
    assert roughness[1]['SphereSurface'] == roughness[0]['SphereSurface']
    assert roughness[2] == roughness[1]
    assert roughness[3] == {'CubeSurface':.25, 'SphereSurface':.75}
    assert roughness[4] == roughness[3]
    assert captures[0]['view']['uv_triangles'] != captures[2]['view']['uv_triangles']
    assert captures[0]['view']['uv_triangles'] == captures[4]['view']['uv_triangles']
    for before, after in [('cube-initial','cube-edited'), ('sphere-initial','sphere-edited')]:
        delta=ImageChops.difference(images[before],images[after])
        for pane in captures[0]['view']['panes']:
            x,y,w,h=pane['rect']
            change=sum(ImageStat.Stat(delta.crop((x,y,x+w,y+h))).mean)/(3*255)
            if pane['view']=='3D': assert change>.0001,(after,change)
            else: assert change<.0001,(after,change)
    result['texture_sets_demo'] = {'pid':process.pid, 'selection':expected_selection,
        'roughness':roughness, 'uv_triangles':[c['view']['uv_triangles'] for c in captures],
        'layer_identities_preserved':True, 'presentation_error':errors}
if a.channels:
    demo = json.loads((a.output/'channels.json').read_text())
    assert demo['pid'] == process.pid
    captures = {c['stage']:c for c in demo['captures']}
    expected_stages = ['initial','metallic-authored','metallic-removed','metallic-added',
        'roughness-removed','undo','redo','undo-again','branch','redo-discarded','sphere-switch','cube-restored','scattering-added','scattering-half','scattering-undo',
        'scattering-redo','scattering-removed','scattering-restored']
    assert list(captures) == expected_stages
    def texture(name):
        return next(t for t in captures[name]['document']['texture_sets'] if t['name']=='CubeSurface')
    def material(name):
        return next(m for m in captures[name]['native']['materials'] if m['name']=='CubeSurface')
    initial_sphere = next(t for t in doc['texture_sets'] if t['name']=='SphereSurface')
    errors, images = {}, {}
    for stage,c in captures.items():
        assert c['document']['id'] == doc['id']
        assert next(t for t in c['document']['texture_sets'] if t['name']=='SphereSurface') == initial_sphere
        if expected_stages.index(stage) < 12:
            assert texture(stage)['layers'][0] == texture('initial')['layers'][0]
        assert [l['id'] for l in texture(stage)['layers']]==[l['id'] for l in texture('initial')['layers']]
        assert c['native']['mesh_objects'] == 2
        image = Image.open(a.output/f'{stage}-viewport.png').convert('RGB')
        images[stage] = image
        window = Image.open(a.output/f'{stage}-window.png').convert('RGB')
        x,y,w,h = c['viewport_rect']
        errors[stage] = sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage] < .02
    assert math.isclose(material('metallic-authored')['metallic'], .8, abs_tol=1e-6)
    assert texture('metallic-removed')['channels'] == [True,False,True,True,True]
    assert material('metallic-removed')['metallic'] == 0
    assert texture('metallic-removed')['base_values'] == texture('metallic-authored')['base_values']
    assert texture('metallic-removed')['layers'] == texture('metallic-authored')['layers']
    assert not captures['metallic-removed']['workspace']['metallic_visible']
    assert texture('metallic-added') == texture('metallic-authored')
    assert captures['metallic-added']['workspace']['metallic_visible']
    assert material('metallic-added') == material('metallic-authored')
    assert texture('roughness-removed')['channels'] == [True,True,False,True,True]
    assert math.isclose(material('roughness-removed')['roughness'], .3, abs_tol=1e-6)
    assert not captures['roughness-removed']['workspace']['roughness_visible']
    for before,after in [('metallic-added','undo'),('roughness-removed','redo'),('undo','undo-again'),('branch','redo-discarded')]:
        assert captures[before]['document'] == captures[after]['document']
        assert captures[before]['native'] == captures[after]['native']
    assert math.isclose(material('branch')['metallic'], .2, abs_tol=1e-6)
    assert captures['sphere-switch']['workspace']['selected_material_name'] == 'SphereSurface'
    assert captures['cube-restored']['workspace']['selected_material_name'] == 'CubeSurface'
    assert material('cube-restored') == material('branch')
    change = sum(ImageStat.Stat(ImageChops.difference(images['metallic-authored'],images['metallic-removed'])).mean)/(3*255)
    assert change > .002, 'Removing a nonzero channel did not change native EEVEE'
    for stage in expected_stages[12:]:
        scalar=texture(stage)['users'][0]
        assert scalar['scattering'] and scalar['name']=='Scattering' and scalar['allocated']
        assert scalar['present']==(stage!='scattering-removed')
        assert scalar['precision']==(1 if stage in ['scattering-added','scattering-undo'] else 5)
        assert material(stage)==material('branch'), 'Scattering must not alter the current PBR shader'
        assert [l['id'] for l in texture(stage)['layers']]==[l['id'] for l in texture('initial')['layers']]
    assert texture('scattering-half')==texture('scattering-redo')==texture('scattering-restored')
    assert texture('scattering-added')==texture('scattering-undo')
    result['channels_demo'] = {'pid':process.pid, 'stages':expected_stages,
        'scattering_l8_add_l16f_format_undo_and_retained_readd':True,
        'authored_values_retained':True, 'independent_sets':True, 'undo_redo_branch':True,
        'metallic_removal_difference':change, 'presentation_error':errors}
if a.color:
    demo = json.loads((a.output/'color.json').read_text())
    assert demo['pid'] == process.pid
    captures = {c['stage']:c for c in demo['captures']}
    assert list(captures) == ['aces','standard','restored']
    errors, images, conversion_errors = {}, {}, {}
    initial = captures['aces']
    for stage,c in captures.items():
        assert c['document'] == doc, 'Display change mutated serialized authored values'
        color = c['color']
        assert color['working_space'] == color['active_working_space'] == 'ACEScg'
        assert color['native_acescg']
        assert color['scene_linear_to_xyz'] == initial['color']['scene_linear_to_xyz']
        assert color['view_transform'] == ('Standard' if stage=='standard' else 'ACES 1.3')
        assert c['workspace']['display_transform'] == color['view_transform']
        for mat in c['native']['materials']:
            original = next(m for m in initial['native']['materials'] if m['name']==mat['name'])
            for key in ['linear_base_color','metallic','roughness']:
                assert mat[key] == original[key], (stage,key)
            expected = color_expected[mat['name']]
            error = max(abs(a-b) for a,b in zip(mat['linear_base_color'],expected))
            assert error < 5e-5, (mat['name'],mat['linear_base_color'],expected)
            conversion_errors[mat['name']] = error
            assert max(abs(a-b) for a,b in zip(expected,source_colors[mat['name']])) > .02
            authored = next(t for t in doc['texture_sets'] if t['name']==mat['name'])['base_values']['color']
            assert authored == mat['linear_base_color'], 'Serialized color differs from native authored color'
        image = Image.open(a.output/f'{stage}-viewport.png').convert('RGB')
        images[stage]=image
        window = Image.open(a.output/f'{stage}-window.png').convert('RGB')
        x,y,w,h = c['viewport_rect']
        errors[stage] = sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage] < .02
    difference = sum(ImageStat.Stat(ImageChops.difference(images['aces'],images['standard'])).mean)/(3*255)
    restored = sum(ImageStat.Stat(ImageChops.difference(images['aces'],images['restored'])).mean)/(3*255)
    assert difference > .01 and restored < .005
    result['color_demo'] = {'pid':process.pid, 'stages':list(captures), 'authored_values_preserved':True,
        'native_acescg':True, 'independent_ocio_conversion_errors':conversion_errors,
        'display_difference':difference, 'restore_difference':restored, 'presentation_error':errors}
if a.fill:
    demo = json.loads((a.output/'fill.json').read_text())
    assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['base','first-fill','first-edited','second-fill','second-edited','lower-selected',
            'upper-hidden','upper-shown','reordered','undo-order','other-set']
    assert list(captures)==stages
    def sphere(stage):
        return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='SphereSurface')
    def native_roughness(stage,name='SphereSurface'):
        return next(m for m in captures[stage]['native']['materials'] if m['name']==name)['roughness']
    first=sphere('first-fill')['layers'][-1]
    second=sphere('second-fill')['layers'][-1]
    assert first['kind']=='fill' and first['name']=='Fill layer 1'
    assert second['name']=='Fill layer 2' and second['id']!=first['id']
    for layer in [first,second]:
        assert layer['visible'] and layer['channel_opacity']==[1]*5 and layer['coverage']=='full'
        assert layer['channels']==[True]*5
        assert math.isclose(layer['values']['roughness'],.3,abs_tol=1e-6)
    expected_roughness=[0,.3,.8,.3,.2,.2,.8,.2,.8,.2,.2]
    cube=next(t for t in doc['texture_sets'] if t['name']=='CubeSurface')
    errors,images={},{}
    for stage,roughness in zip(stages,expected_roughness):
        c=captures[stage]
        assert math.isclose(native_roughness(stage),roughness,abs_tol=1e-6),stage
        assert native_roughness(stage,'CubeSurface')==0
        assert next(t for t in c['document']['texture_sets'] if t['name']=='CubeSurface')==cube
        assert c['native']['mesh_objects']==2
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB')
        x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage]<.02
    assert math.isclose(captures['lower-selected']['workspace']['roughness'],.8,abs_tol=1e-6)
    for stage in ['lower-selected','upper-hidden','upper-shown','reordered','undo-order']:
        assert sphere(stage)['selected_layer']==first['id']
        assert captures[stage]['workspace']['selected_layer_id'].strip('{}')==first['id']
    assert sphere('upper-hidden')['layers'][-1]['visible'] is False
    assert sphere('upper-hidden')['layers'][-1]['values']==sphere('upper-shown')['layers'][-1]['values']
    assert [l['id'] for l in sphere('reordered')['layers']][-2:]==[second['id'],first['id']]
    assert sphere('undo-order')==sphere('upper-shown')
    changes={}
    for before,after in [('first-fill','first-edited'),('upper-hidden','upper-shown'),('upper-shown','reordered')]:
        changes[after]=sum(ImageStat.Stat(ImageChops.difference(images[before],images[after])).mean)/(3*255)
        assert changes[after]>.002,(before,after)
    result['fill_demo']={'pid':process.pid,'stages':stages,'native_roughness':expected_roughness,
        'independent_sets':True,'selection_independent_composition':True,'visibility_preserves_values':True,
        'reorder_and_undo':True,'image_differences':changes,'presentation_error':errors}
if a.inspection:
    demo=json.loads((a.output/'inspection.json').read_text())
    assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['material','base-color','metallic','roughness','rough-standard','rough-edited','hidden','shown','normal','normal-aces','height','material-restored']
    assert list(captures)==stages
    assert [c['inspection'] for c in captures.values()]==[-1,0,1,2,2,2,2,2,3,3,4,-1]
    for group in [stages[:5], ['rough-edited','shown','normal','normal-aces','height','material-restored']]:
        first=captures[group[0]]
        for stage in group[1:]:
            assert captures[stage]['document']==first['document'],stage
            def authored_native(capture):
                native=json.loads(json.dumps(capture['native']))
                for material in native['materials']: material.pop('display_base_color')
                return native
            assert authored_native(captures[stage])==authored_native(first),stage
    errors,probes,images={},{},{}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB')
        x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage]<.02
        x,y,w,h=next(p['rect'] for p in c['view']['panes'] if p['view']=='2D')
        cx,cy=x+w//2,y+h//2
        probes[stage]=ImageStat.Stat(image.crop((cx-8,cy-8,cx+8,cy+8))).mean
    for stage,expected in {'metallic':[0]*3,'roughness':[76.5]*3,'rough-standard':[76.5]*3,
        'rough-edited':[204]*3,'shown':[204]*3,'normal':[127.5,127.5,255],
        'normal-aces':[127.5,127.5,255],'height':[0]*3}.items():
        assert all(abs(x-y)<2 for x,y in zip(probes[stage],expected)),(stage,probes[stage],expected)
    assert all(35<v<90 for v in probes['hidden']),probes['hidden']
    differences={}
    for before,after in [('roughness','rough-standard'),('normal','normal-aces')]:
        # Exclude viewport labels/chrome; compare interiors in both native render panes.
        values=[]
        for pane in captures[before]['view']['panes']:
            x,y,w,h=pane['rect'];box=(x+20,y+40,x+w-20,y+h-40)
            values.append(sum(ImageStat.Stat(ImageChops.difference(images[before].crop(box),images[after].crop(box))).mean)/(3*255))
        assert max(values)<.002,(before,after,values)
        differences[after]=values
    # Material mode is a flat Base Color texture plane, unaffected by roughness or HDRI.
    material=captures['material']
    x,y,w,h=next(p['rect'] for p in material['view']['panes'] if p['view']=='2D')
    cx,cy=x+w//2,y+h//2
    region=(cx-40,cy-40,cx+40,cy+40)
    flat_stddev=ImageStat.Stat(images['material'].crop(region)).stddev
    assert max(flat_stddev)<1,flat_stddev
    flat_difference=sum(ImageStat.Stat(ImageChops.difference(images['material'].crop(region),images['material-restored'].crop(region))).mean)/(3*255)
    assert flat_difference<.0001,flat_difference
    # Tile covers 0..1 continuously, including outside the sphere's packed UV island.
    state=material['view'];extent=state['uv_vertical_extent'];center=state['uv_center']
    tile_samples=[]
    for u,v in [(0.1,0.4),(0.8,0.4),(0.8,0.8)]:
        px=round(x+w/2+(u-center[0])*h/extent);py=round(y+h/2-(v-center[1])*h/extent)
        tile_samples.append(images['material'].getpixel((px,py)))
    assert max(max(rgb) for rgb in tile_samples)-min(min(rgb) for rgb in tile_samples)<=1,tile_samples
    result['inspection_demo']={'pid':process.pid,'flat_uv_stddev':flat_stddev,
        'flat_uv_roughness_difference':flat_difference,'tile_samples':tile_samples,'stages':stages,'authored_values_preserved':True,
        'data_pixel_probes':probes,'display_transform_differences':differences,'presentation_error':errors}
if a.paint:
    demo=json.loads((a.output/'paint.json').read_text())
    assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['empty','small-3d','large-3d','rough-disabled','metal-stroke','metal-view','undo-stroke','redo-stroke','uv-stroke','cancelled','layer-hidden','layer-shown','other-set','material']
    if a.project_roundtrip: stages += ['saved','reopened','edited-reopened']
    assert list(captures)==stages
    def sphere(stage): return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='SphereSurface')
    def layer(stage): return sphere(stage)['layers'][-1]
    empty=layer('empty');assert empty['kind']=='paint' and empty['name']=='Layer 2' and empty['coverage']=='empty'
    assert layer('large-3d')['strokes']['operations']==2 and layer('large-3d')['strokes']['spatial']==2
    assert layer('uv-stroke')['strokes']['operations']==3 and layer('uv-stroke')['strokes']['spatial']==2

    for stage in ['small-3d','large-3d','metal-stroke','uv-stroke']:
        assert layer(stage)['coverage']=='painted' and all(n>0 for n in layer(stage)['paint_pages']),stage
    assert all(a!=b for a,b in zip(empty['paint_hashes'],layer('small-3d')['paint_hashes']))
    assert captures['rough-disabled']['document']==captures['large-3d']['document']
    assert layer('metal-stroke')['paint_hashes'][2]==layer('large-3d')['paint_hashes'][2]
    assert layer('metal-stroke')['paint_hashes'][1]!=layer('large-3d')['paint_hashes'][1]
    assert captures['undo-stroke']['document']==captures['large-3d']['document']
    assert captures['redo-stroke']['document']==captures['metal-stroke']['document']
    assert captures['cancelled']['document']==captures['uv-stroke']['document']
    assert captures['undo-stroke']['workspace']['redo_enabled']
    assert not captures['uv-stroke']['workspace']['redo_enabled']
    assert captures['large-3d']['workspace']['brush_size']==30
    assert captures['large-3d']['workspace']['toolbar_brush_size']==30
    assert not captures['rough-disabled']['workspace']['roughness_visible']
    assert captures['layer-shown']['document']==captures['uv-stroke']['document']
    assert layer('layer-hidden')['paint_hashes']==layer('layer-shown')['paint_hashes']
    cube=next(t for t in captures['empty']['document']['texture_sets'] if t['name']=='CubeSurface')
    errors,images,white={},{},{}
    for stage,c in captures.items():
        assert next(t for t in c['document']['texture_sets'] if t['name']=='CubeSurface')==cube
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage]<.02
        if stage in ['empty','small-3d','large-3d']:
            white[stage]={}
            for pane in c['view']['panes']:
                x,y,w,h=pane['rect'];region=image.crop((x+40,y+50,x+w-40,y+h-50))
                white[stage][pane['view']]=sum(min(rgb)>220 for rgb in region.getdata())
    for view in ['3D','2D']:
        small=white['small-3d'][view]-white['empty'][view]
        large=white['large-3d'][view]-white['small-3d'][view]
        assert small>50 and large>small*1.5,(view,small,large)
    differences={}
    for before,after in [('metal-view','undo-stroke'),('undo-stroke','redo-stroke'),('redo-stroke','uv-stroke'),('layer-hidden','layer-shown')]:
        values=[]
        for pane in captures[before]['view']['panes']:
            x,y,w,h=pane['rect'];box=(x+20,y+40,x+w-20,y+h-40)
            values.append(sum(ImageStat.Stat(ImageChops.difference(images[before].crop(box),images[after].crop(box))).mean)/(3*255))
        assert min(values)>.0001,(before,after,values)
        differences[after]=values
    if a.project_roundtrip:
        assert captures['saved']['document']==captures['reopened']['document'], 'Native file roundtrip changed owned pixels or metadata'
        assert captures['saved']['color']==captures['reopened']['color']
        assert not captures['reopened']['workspace']['undo_enabled'] and not captures['reopened']['workspace']['redo_enabled']
        assert captures['reopened']['workspace']['brush_size']==10
        assert layer('reopened')['id']==layer('edited-reopened')['id']
        assert all(a!=b for a,b in zip(layer('reopened')['paint_hashes'],layer('edited-reopened')['paint_hashes']))
        assert (a.output/'painted.blend').stat().st_size>10000
        result['project_roundtrip']={'bit_exact_pixels':True,'stable_layer_ids':True,'session_history_cleared':True,'same_layer_edited_after_open':True}
    result['paint_demo']={'pid':process.pid,'stages':stages,'underlying_fill':not a.no_fill,'multichannel_atomic_undo':True,
        'disabled_channel_preserved':True,'independent_sets':True,'cancel_restores_stroke':True,'redo_branch_discarded':True,
        'white_stroke_pixels':white,'image_differences':differences,'presentation_error':errors}
if a.flow:
    demo=json.loads((a.output/'flow.json').read_text())
    assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['empty','flow20','flow100','flow0','zero-stroke','repeat20','undo-repeat','redo-repeat','uv20']
    def layer(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='SphereSurface')['layers'][-1]
    for stage,value in [('flow20',20),('flow100',100),('flow0',0),('zero-stroke',0),('repeat20',20)]:
        assert captures[stage]['workspace']['brush_flow']==captures[stage]['workspace']['toolbar_brush_flow']==value
    for stage in captures:
        assert all(layer(stage)['paint_pages'][c]==0 for c in [0,1,3,4])
    for a_stage,b_stage in [('flow100','flow0'),('flow100','zero-stroke'),('flow100','undo-repeat'),('repeat20','redo-repeat')]:
        assert captures[a_stage]['document']==captures[b_stage]['document']
    for a_stage,b_stage in [('empty','flow20'),('flow20','flow100'),('zero-stroke','repeat20'),('redo-repeat','uv20')]:
        assert layer(a_stage)['paint_hashes'][2]!=layer(b_stage)['paint_hashes'][2]
    errors,images,peaks={},{},{}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255)
        assert errors[stage]<.02
        scene=next(p for p in c['view']['panes'] if p['view']=='3D');x,y,w,h=scene['rect']
        peaks[stage]=max(min(rgb) for rgb in image.crop((x+40,y+40,x+w-40,y+h-40)).getdata())
    assert peaks['flow20']>peaks['empty']+20
    assert peaks['flow100']>peaks['flow20']+5,(peaks['flow20'],peaks['flow100'])
    accumulation=sum(ImageStat.Stat(ImageChops.difference(images['repeat20'],images['zero-stroke'])).mean)/(3*255)
    assert accumulation>.0001
    result['flow_demo']={'pid':process.pid,'stages':list(captures),'peak_gray':peaks,
        'repeat_accumulation_difference':accumulation,'zero_flow_is_noop':True,'only_enabled_channel_written':True,
        'toolbar_inspector_synchronized':True,'existing_marks_preserved':True,'undo_redo':True,'presentation_error':errors}
if a.opacity:
    demo=json.loads((a.output/'opacity.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['empty','overlap20','second20','setting100','full100','setting0','zero-stroke','undo-full','redo-full','cancelled']
    for stage,value in [('overlap20',20),('second20',20),('setting100',100),('full100',100),('setting0',0),('zero-stroke',0)]:
        assert captures[stage]['workspace']['stroke_opacity']==captures[stage]['workspace']['toolbar_stroke_opacity']==value
    for stage,expected in [('empty',0),('overlap20',.2),('second20',.36)]:
        pixel=captures[stage]['center_roughness_pixel'];assert abs(pixel[0]-expected)<.001 and abs(pixel[3]-expected)<.001,(stage,pixel)
    for x,y in [('second20','setting100'),('full100','setting0'),('full100','zero-stroke'),('second20','undo-full'),('full100','redo-full'),('full100','cancelled')]:
        assert captures[x]['document']==captures[y]['document'],(x,y)
    errors,peaks={},{}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
        p=next(p for p in c['view']['panes'] if p['view']=='2D');x,y,w,h=p['rect']
        peaks[stage]=max(min(rgb) for rgb in image.crop((round(x+w*.2),round(y+h*.35),round(x+w*.8),round(y+h*.8))).getdata())
    assert abs(peaks['overlap20']-(.3+.7*.2)*255)<3
    assert abs(peaks['second20']-(.3+.7*.36)*255)<3
    assert peaks['full100']>250
    result['opacity_demo']={'pid':process.pid,'stages':list(captures),'center_coverage':{s:captures[s]['center_roughness_pixel'][3] for s in ['overlap20','second20']},
        'uv_peak_gray':peaks,'within_stroke_cap':True,'between_stroke_accumulation':True,
        'zero_noop_undo_redo_cancel':True,'toolbar_inspector_synchronized':True,'presentation_error':errors}
if a.spacing:
    demo=json.loads((a.output/'spacing.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['empty','dense20','setting150','sparse150','undo-sparse','redo-sparse','undo-again','coarse150','scene150']
    for x,y in [('dense20','setting150'),('dense20','undo-sparse'),('sparse150','redo-sparse'),('dense20','undo-again')]:
        assert captures[x]['document']==captures[y]['document']
    for stage in ['setting150','sparse150','coarse150']:
        assert captures[stage]['workspace']['brush_spacing']==captures[stage]['workspace']['toolbar_brush_spacing']==150
    images,errors={},{}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def runs(stage,y_fraction):
        p=next(p for p in captures[stage]['view']['panes'] if p['view']=='2D');x,y,w,h=p['rect']
        line=[min(images[stage].getpixel((px,round(y+h*y_fraction))))>160 for px in range(round(x+w*.2),round(x+w*.8))]
        # UV island contours intentionally overlay the texture; bridge their one/two-pixel cuts.
        for i in range(1,len(line)-2):
            if line[i-1] and not line[i]:
                if line[i+1]:line[i]=True
                elif line[i+2]:line[i]=line[i+1]=True
        return sum(v and (i==0 or not line[i-1]) for i,v in enumerate(line))
    dense,sparse=runs('dense20',.35),runs('sparse150',.65)
    assert dense==1 and sparse>=3,(dense,sparse)
    difference=sum(ImageStat.Stat(ImageChops.difference(images['sparse150'],images['coarse150'])).mean)/(3*255)
    assert difference<.00001,difference
    assert captures['scene150']['document']!=captures['coarse150']['document']
    result['spacing_demo']={'pid':process.pid,'stages':list(captures),'dense_runs':dense,'sparse_runs':sparse,'ignored_uv_contour_gap_pixels':2,
        'event_segmentation_image_difference':difference,'existing_marks_preserved':True,'undo_redo':True,
        'three_dimensional_stamps':True,'toolbar_inspector_synchronized':True,'presentation_error':errors}
if a.pressure:
    demo=json.loads((a.output/'pressure.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['defaults','size-low','undo-low','size-high','independent-toggles','undo-high','flow-low','undo-flow','flow-high','both-low','cancelled','minimums-retained','mouse-full','ramp']
    for stage,c in captures.items():
        w=c['workspace'];assert w['pressure_size']==w['toolbar_pressure_size'] and w['pressure_flow']==w['toolbar_pressure_flow']
        assert w['minimum_size_enabled']==w['pressure_size'] and w['minimum_flow_enabled']==w['pressure_flow']
    assert captures['defaults']['workspace']['pressure_size'] and not captures['defaults']['workspace']['pressure_flow']
    assert not captures['independent-toggles']['workspace']['pressure_size'] and captures['independent-toggles']['workspace']['pressure_flow']
    assert captures['minimums-retained']['workspace']['minimum_size']==20 and captures['minimums-retained']['workspace']['minimum_flow']==10
    assert not captures['minimums-retained']['workspace']['minimum_size_enabled'] and not captures['minimums-retained']['workspace']['minimum_flow_enabled']
    for x,y in [('defaults','undo-low'),('defaults','undo-high'),('defaults','undo-flow'),('size-high','independent-toggles'),('size-high','flow-high'),('both-low','cancelled'),('both-low','minimums-retained'),('size-high','mouse-full')]:
        assert captures[x]['document']==captures[y]['document'],(x,y)
    metrics={s:c['pressure_pixels'] for s,c in captures.items()}
    size_ratio=metrics['size-low']['covered']/metrics['size-high']['covered'];assert .075<size_ratio<.09,size_ratio
    flow_ratio=metrics['flow-low']['sum']/metrics['flow-high']['sum'];assert abs(flow_ratio-.2875)<1e-5,flow_ratio
    assert metrics['flow-low']['covered']==metrics['flow-high']['covered']
    assert metrics['both-low']['covered']==metrics['size-low']['covered']
    assert abs(metrics['both-low']['sum']/metrics['size-low']['sum']-.2875)<1e-5
    assert metrics['ramp']['covered']>metrics['mouse-full']['covered']*2
    errors={}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['pressure_demo']={'pid':process.pid,'stages':list(captures),'input_source':'synthetic QTabletEvent delivered to native viewport',
        'physical_tablet_verified':False,'pressure_curve':'product linear interpolation with independent minimums',
        'size_area_ratio':size_ratio,'flow_coverage_ratio':flow_ratio,'metrics':metrics,
        'independent_toggles_and_minimums':True,'mouse_full_pressure_equivalent':True,'ramp_undo_cancel':True,'presentation_error':errors}
if a.surface or a.surface_volume:
    demo=json.loads((a.output/'surface.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};assert list(captures)==['empty','surface-stroke','undo','redo']
    assert captures['empty']['document']==captures['undo']['document']
    assert captures['surface-stroke']['document']==captures['redo']['document']
    metrics=captures['surface-stroke']['islands']
    for i,channels in enumerate(metrics):
        for c,channel in enumerate(channels):
            if a.surface_volume:
                assert (channel['covered']>1000 if c==2 and i in [0,1,2,3,5] else channel['covered']==0),(i,c,channel)
            else:assert (channel['covered']>1000 if i==0 else channel['covered']==0),(i,channel)
    assert min(captures['surface-stroke']['hidden_base_alpha'])>.2
    for c in captures['empty']['islands']+captures['undo']['islands']:
        assert all(v['covered']==0 for v in c)
    errors={}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['surface_demo']={'pid':process.pid,'stages':list(captures),'islands':metrics,
        'hidden_base_alpha':captures['surface-stroke']['hidden_base_alpha'],'all_channels_atomic_undo_redo':True,
        'policy':('culling off: nearby disconnected sheets admitted, far sheet rejected; reference APP-003' if a.surface_volume else 'culling on: connected selected-material faces and angle filter'),
        'presentation_error':errors}
if a.udim:
    demo=json.loads((a.output/'udim.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};assert list(captures)==['empty','color','roughness','undo','redo','saved','reopened','edited']
    def tiles(stage):return captures[stage]['document']['texture_sets'][0]['layers'][0]['paint_tiles']
    for c in captures.values():assert c['document']['texture_sets'][0]['tiles']==[1001,1002]
    assert captures['empty']['document']==captures['undo']['document']
    for stage in ['roughness','redo','saved','reopened']:assert captures['color']['document']==captures[stage]['document'],stage
    assert [t['number'] for t in tiles('color')]==[1001,1002]
    for t in tiles('color'):
        assert t['pages'][0]>0 and t['pages'][2]>0 and t['pages'][1]==t['pages'][3]==t['pages'][4]==0
    for before,after in zip(tiles('reopened'),tiles('edited')):
        for c in [0,2]:assert before['hashes'][c]!=after['hashes'][c]
        for c in [1,3,4]:assert after['pages'][c]==0
    probes=captures['roughness']['tile_samples']
    for tile in probes:
        for c in [0,2]:assert tile['seam'][c][3]>.9,tile
        assert abs(tile['seam'][2][0]/tile['seam'][2][3]-.3)<1e-5
    assert abs(probes[0]['seam'][2][3]-probes[1]['seam'][2][3])<.02
    assert not captures['reopened']['workspace']['undo_enabled'] and not captures['reopened']['workspace']['redo_enabled']
    errors={}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert (a.output/'painted.blend').stat().st_size>10000 and (a.output/'edited.blend').stat().st_size>10000
    result['udim_demo']={'pid':process.pid,'stages':list(captures),'tiles':[1001,1002],
        'seam_samples':probes,'multichannel_atomic_undo_redo':True,'exact_save_reopen':True,
        'same_layer_edit_both_tiles':True,'presentation_error':errors}
if a.environment_demo:
    demo=json.loads((a.output/'environment.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial90','angle0','restored90','angle30','saved','reopened','rough30','rough-minus30']
    angles=[90,0,90,30,30,30,30,-30]
    for c,angle in zip(captures.values(),angles):
        assert abs(c['environment']['rotation']-angle)<1e-4
        assert c['document']==captures['initial90']['document']
    for stage in ['angle0','restored90','angle30','saved']:
        assert captures[stage]['native']==captures['initial90']['native']
        assert captures[stage]['view']==captures['initial90']['view']
    assert captures['rough30']['view']==captures['rough-minus30']['view']
    errors={};images={}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def difference(a,b,kind):
        x,y,w,h=next(p['rect'] for p in captures[a]['view']['panes'] if p['view']==kind)
        box=(x+20,y+45,x+w-20,y+h-40)
        return sum(ImageStat.Stat(ImageChops.difference(images[a].crop(box),images[b].crop(box))).mean)/(3*255)
    changed=difference('initial90','angle0','3D');assert changed>.001,changed
    restored=difference('initial90','restored90','3D');assert restored<.002,restored
    uv_differences={stage:difference('initial90',stage,'2D') for stage in ['angle0','restored90','angle30']}
    assert max(uv_differences.values())<.0001,uv_differences
    data_differences={kind:difference('rough30','rough-minus30',kind) for kind in ['3D','2D']}
    assert max(data_differences.values())<.0001,data_differences
    assert (a.output/'environment.blend').stat().st_size>10000
    result['environment_demo']={'pid':process.pid,'stages':list(captures),'angles':angles,
        'geometry_camera_and_authored_document_unchanged':True,'saved_angle_reopened':True,
        'material_3d_difference':changed,'restored_3d_difference':restored,
        'flat_uv_differences':uv_differences,'raw_data_differences':data_differences,'presentation_error':errors}
if a.appearance:
    demo=json.loads((a.output/'appearance.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','exposure2','exposure0','opacity100','blur0','background-restored','custom','saved','reopened','rough','rough-exposure']
    expected=[(0,0,50),(2,0,50),(0,0,50),(0,100,50),(0,100,0),(0,0,50),(1,40,15),(1,40,15),(1,40,15),(1,0,15),(-1,0,15)]
    images={};errors={}
    for (stage,c),values in zip(captures.items(),expected):
        assert all(abs(c['environment'][key]-value)<1e-4 for key,value in zip(['exposure','opacity','blur'],values))
        assert c['document']==captures['initial']['document']
        if stage not in ['reopened','rough','rough-exposure']:
            assert c['native']==captures['initial']['native'] and c['view']==captures['initial']['view']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def box(kind):
        x,y,w,h=next(p['rect'] for p in captures['initial']['view']['panes'] if p['view']==kind)
        return x+20,y+45,x+w-20,y+h-40
    def difference(a,b,kind,mask=None):
        return sum(ImageStat.Stat(ImageChops.difference(images[a].crop(box(kind)),images[b].crop(box(kind))),mask).mean)/(3*255)
    initial=images['initial'].crop(box('3D'))
    # The approved zero-opacity canvas is 63/64 gray; isolate bright mesh interiors.
    mask=initial.convert('L').point(lambda v:255 if v>85 else 0).filter(ImageFilter.MinFilter(9))
    assert sum(mask.histogram()[1:])>1000
    changed=difference('initial','exposure2','3D',mask);assert changed>.01,changed
    object_diff={stage:difference('initial',stage,'3D',mask) for stage in ['opacity100','blur0','background-restored']}
    assert max(object_diff.values())<.005,object_diff
    background_diff={'opacity':difference('initial','opacity100','3D'),'blur':difference('opacity100','blur0','3D')}
    assert min(background_diff.values())>.001,background_diff
    uv_diff={stage:difference('initial',stage,'2D') for stage in ['exposure2','opacity100','blur0','background-restored','custom']}
    assert max(uv_diff.values())<.0001,uv_diff
    data_diff={kind:difference('rough','rough-exposure',kind) for kind in ['3D','2D']};assert max(data_diff.values())<.0001,data_diff
    result['appearance_demo']={'pid':process.pid,'stages':list(captures),'native_values_saved_reopened':True,
        'geometry_camera_and_authored_document_unchanged':True,'exposure_object_difference':changed,
        'opacity_blur_object_differences':object_diff,'background_differences':background_diff,
        'flat_uv_differences':uv_diff,'data_differences':data_diff,'presentation_error':errors}
if a.resource:
    demo=json.loads((a.output/'resource.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','alternative','rejected','saved','reopened','default-restored']
    images={};errors={}
    for stage,c in captures.items():
        name=a.environment.name if stage in ['initial','default-restored'] else a.resource.name
        assert c['environment']['resource']==name,(stage,c['environment'])
        assert c['document']==captures['initial']['document']
        if stage in ['alternative','rejected','saved']:assert c['native']==captures['initial']['native'] and c['view']==captures['initial']['view']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def difference(a,b,kind):
        x,y,w,h=next(p['rect'] for p in captures[a]['view']['panes'] if p['view']==kind);box=(x+20,y+45,x+w-20,y+h-40)
        return sum(ImageStat.Stat(ImageChops.difference(images[a].crop(box),images[b].crop(box))).mean)/(3*255)
    changed=difference('initial','alternative','3D');assert changed>.01,changed
    unchanged=difference('initial','alternative','2D');assert unchanged<.0001,unchanged
    rejected=difference('alternative','rejected','3D');assert rejected<.005,rejected
    restored=difference('initial','default-restored','3D');assert restored<.005,restored
    result['resource_demo']={'pid':process.pid,'stages':list(captures),'resource':a.resource.name,
        'selected_bytes_sha256':hashlib.sha256(a.resource.read_bytes()).hexdigest(),'invalid_hdr_preserves_active_environment':True,
        'geometry_camera_authored_data_unchanged':True,'saved_resource_reopened':True,
        'lighting_difference':changed,'flat_uv_difference':unchanged,'rejected_difference':rejected,'restored_difference':restored,'presentation_error':errors}
if a.alignment:
    demo=json.loads((a.output/'alignment.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['world90','world0','local0','local90','world90-restored','local30','saved','reopened','orbit-local','orbit-world','rough-world','rough-local'];assert list(captures)==stages
    expected=['World','World','Local','Local','World','Local','Local','Local','Local','World','World','Local']
    angles=[90,0,0,90,90,30,30,30,30,30,30,30];images={};errors={}
    for (stage,c),alignment,angle in zip(captures.items(),expected,angles):
        assert c['environment']['alignment']==alignment and abs(c['environment']['rotation']-angle)<1e-4
        assert c['document']==captures['world90']['document']
        if stages.index(stage)<7:assert c['native']==captures['world90']['native'] and c['view']==captures['world90']['view']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert captures['orbit-local']['view']==captures['orbit-world']['view']
    assert captures['rough-world']['view']==captures['rough-local']['view']
    assert captures['orbit-local']['view']['scene_view_matrix']!=captures['reopened']['view']['scene_view_matrix']
    assert abs(math.cos(math.radians(captures['orbit-local']['view']['scene_pitch'])))<.95
    def difference(a,b,kind):
        x,y,w,h=next(p['rect'] for p in captures[a]['view']['panes'] if p['view']==kind);box=(x+20,y+45,x+w-20,y+h-40)
        return sum(ImageStat.Stat(ImageChops.difference(images[a].crop(box),images[b].crop(box))).mean)/(3*255)
    changed={'at_zero':difference('world0','local0','3D'),'tilted_camera':difference('orbit-local','orbit-world','3D')}
    assert min(changed.values())>.001,changed
    restored=difference('world90','world90-restored','3D');assert restored<.005,restored
    uv_diff={stage:difference('world90',stage,'2D') for stage in ['world0','local0','local90','local30','orbit-local','orbit-world']}
    assert max(uv_diff.values())<.0001,uv_diff
    data_diff={kind:difference('rough-world','rough-local',kind) for kind in ['3D','2D']};assert max(data_diff.values())<.0001,data_diff
    result['alignment_demo']={'pid':process.pid,'stages':stages,'native_alignment_saved_reopened':True,
        'reference_scope':'modern documented World/Local contract; not observed in Painter 6.2.2',
        'engine':'native EEVEE world-Z/view-space lighting; view basis maps local rotation axis to camera up',
        'lighting_differences':changed,'restored_difference':restored,'flat_uv_differences':uv_diff,'data_differences':data_diff,
        'authored_document_unchanged':True,'alignment_does_not_move_camera':True,'presentation_error':errors}
if a.layer_context:
    demo=json.loads((a.output/'layer-context.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['color','roughness','normal','height','color-restored','raw-roughness','color-in-roughness','removed','other-set','returned']
    expected=['Base Color','Roughness','Roughness','Bump','Base Color','Roughness','Base Color','Metallic','Metallic','Metallic']
    first=captures['color'];errors={}
    for index,((stage,c),context) in enumerate(zip(captures.items(),expected)):
        assert c['workspace']['layer_context']==context,(stage,c['workspace'])
        assert c['workspace']['brush_channels']==first['workspace']['brush_channels']
        assert c['inspection']==(-1 if index<5 else 2)
        if index<7:assert c['document']==first['document'] and c['native']==first['native'] and c['view']==first['view']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB')
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    hashes={stage:c['workspace']['layer_thumbnail_hashes'] for stage,c in captures.items()}
    assert len({hashes[stage][0] for stage in stages[:4]})==3
    assert len({hashes[stage][1] for stage in stages[:7]})==1 # empty paint remains checker
    assert hashes['color']==hashes['color-restored']==hashes['color-in-roughness']
    assert 'Base Color' not in captures['removed']['workspace']['layer_context_options']
    assert 'Base Color' in captures['other-set']['workspace']['layer_context_options']
    assert captures['returned']['document']==captures['removed']['document']
    image_changes={}
    for a_stage,b_stage in [('color','roughness'),('color','normal'),('color','height'),('raw-roughness','color-in-roughness')]:
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        image_changes[b_stage]=sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    assert max(image_changes.values())<.005,image_changes
    result['layer_context_demo']={'pid':process.pid,'stages':stages,'channel_thumbnail_hashes':hashes,
        'brush_channels_unchanged':True,'viewport_mode_independent':True,'available_channels_follow_selected_set':True,
        'document_unchanged_by_context':True,'viewport_differences':image_changes,'presentation_errors':errors}
if a.layer_opacity:
    demo=json.loads((a.output/'layer-opacity.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','height20','base100','base40','rough100','rough10','undo','redo','saved','reopened','zero-height','undo-zero']
    expected=[[1,1,1,1,1],[1,1,1,1,.2],[1,1,1,1,.2],[.4,1,1,1,.2],[.4,1,1,1,.2],[.4,1,.1,1,.2],[.4,1,1,1,.2],[.4,1,.1,1,.2],[.4,1,.1,1,.2],[.4,1,.1,1,.2],[.4,1,.1,1,0],[.4,1,.1,1,.2]]
    first=captures['initial'];errors={}
    for (stage,c),opacity in zip(captures.items(),expected):
        layer=c['document']['texture_sets'][1]['layers'][-1]
        assert all(abs(a-b)<1e-6 for a,b in zip(layer['channel_opacity'],opacity)),stage
        actual=c['composite'];assert abs(actual['height']-(.2+.8*opacity[4]))<1e-6
        assert abs(actual['roughness']-(.2+.6*opacity[2]))<1e-6
        assert all(abs(a-b)<1e-6 for a,b in zip(actual['color'],[1-opacity[0],0,opacity[0]]))
        context={'Base Color':0,'Metallic':1,'Roughness':2,'Bump':4}[c['workspace']['layer_context']]
        assert abs(c['row_opacities'][0]-opacity[context]*100)<1e-4
        for key in ['brush_channels','stroke_opacity','brush_flow']:assert c['workspace'][key]==first['workspace'][key]
        assert c['inspection']==4 and c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    for a_stage,b_stage in [('base40','undo'),('rough10','redo'),('saved','reopened'),('reopened','undo-zero')]:assert captures[a_stage]['document']==captures[b_stage]['document'],(a_stage,b_stage)
    image_changes={}
    for a_stage,b_stage in [('initial','height20'),('height20','base40'),('base40','rough10'),('reopened','zero-height')]:
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        image_changes[b_stage]=sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    assert image_changes['height20']>.02 and image_changes['zero-height']>.005,image_changes
    assert image_changes['base40']<.0001 and image_changes['rough10']<.0001,image_changes
    result['layer_opacity_demo']={'pid':process.pid,'stages':stages,'independent_five_channel_values':True,
        'height_blend_20_percent':captures['height20']['composite'],'color40_rough10':captures['rough10']['composite'],
        'unchanged_brush_settings_and_other_set':True,'undo_redo_save_open_exact':True,'height_view_differences':image_changes,'presentation_errors':errors}
if a.layer_drag:
    demo=json.loads((a.output/'layer-drag.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','preview-top','moved-top','undo','redo','preview-bottom','cancelled','preview-bottom-again','moved-bottom','noop','rejected','saved','reopened']
    first=captures['initial'];original=first['document']['texture_sets'][1]['layers'];ids=[l['id'] for l in original];assert len(ids)==4
    top=[ids[0],ids[2],ids[3],ids[1]];bottom=[ids[1],ids[0],ids[2],ids[3]];errors={};indicator_pixels={}
    for index,(stage,c) in enumerate(captures.items()):
        expected=ids if index in [0,1,3] else top if index<8 else bottom
        texture_set=c['document']['texture_sets'][1]
        assert [l['id'] for l in texture_set['layers']]==expected,stage
        assert {l['id']:l for l in texture_set['layers']}=={l['id']:l for l in original}
        assert texture_set['selected_layer']==ids[1]
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        assert c['inspection']==2 and c['workspace']['brush_channels']==first['workspace']['brush_channels']
        assert c['drop_indicator_row']==(0 if index==1 else 4 if index in [5,7] else -1)
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
        if index in [1,5,7]:
            indicator_pixels[stage]=sum(1 for r,g,b in window.getdata() if abs(r-130)<3 and abs(g-182)<3 and abs(b-235)<3)
            assert indicator_pixels[stage]>200,indicator_pixels
    for a_stage,b_stage in [('initial','preview-top'),('moved-top','redo'),('redo','cancelled'),('moved-bottom','noop'),('noop','rejected'),('saved','reopened')]:assert captures[a_stage]['document']==captures[b_stage]['document']
    def difference(a_stage,b_stage):
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        return sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    changed=difference('initial','moved-top');assert changed>.02,changed
    unchanged={stage:difference('moved-top',stage) for stage in ['redo','preview-bottom','cancelled','preview-bottom-again']}
    assert max(unchanged.values())<.0001,unchanged
    result['layer_drag_demo']={'pid':process.pid,'stages':stages,'source':'Qt drag enter/move/drop and leave events through the layer viewport',
        'preview_bar_pixels':indicator_pixels,'stable_layer_ids_and_values':True,'only_drop_mutates_order':True,
        'undo_redo_save_open_exact':True,'noop_and_foreign_drag_preserve_document':True,'reorder_viewport_difference':changed,'preview_cancel_differences':unchanged,'presentation_errors':errors}
if a.folders:
    demo=json.loads((a.output/'folders.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','empty-folder','paint-above','grouped','collapsed','expanded','hidden','shown','opacity50','nested','saved','reopened','child-edited','undo-child']
    first=captures['initial'];original=first['document']['texture_sets'][1]['layers'];child=original[-1]['id'];errors={}
    def texture(stage):return captures[stage]['document']['texture_sets'][1]
    def selected(stage):
        value=texture(stage);return next(l for l in value['layers'] if l['id']==value['selected_layer'])
    empty=selected('empty-folder');assert empty['kind']=='folder' and not empty['parent']
    paint=selected('paint-above');assert paint['kind']=='paint' and not paint['parent']
    group=selected('grouped');assert group['kind']=='folder'
    assert next(l for l in texture('grouped')['layers'] if l['id']==child)['parent']==group['id']
    assert texture('grouped')==texture('collapsed')==texture('expanded')
    assert len(captures['collapsed']['workspace']['layer_rows'])+1==len(captures['expanded']['workspace']['layer_rows'])
    assert any(r['collapsed'] for r in captures['collapsed']['workspace']['layer_rows'])
    nested=selected('nested');assert nested['kind']=='folder' and nested['id']!=group['id']
    assert next(l for l in texture('nested')['layers'] if l['id']==group['id'])['parent']==nested['id']
    assert max(r['depth'] for r in captures['nested']['workspace']['layer_rows'])==2
    expected=[.8,.8,.8,.8,.8,.8,.2,.8,.5,.5,.5,.5,.4,.5]
    for (stage,c),roughness in zip(captures.items(),expected):
        actual=next(m for m in c['native']['materials'] if m['name']=='SphereSurface')['roughness'];assert abs(actual-roughness)<1e-6,(stage,actual)
        assert c['inspection']==2 and c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        if selected(stage)['kind']=='folder':assert not c['workspace']['roughness_visible'] and not c['workspace']['roughness_editable']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert captures['saved']['document']==captures['reopened']['document']
    before=texture('reopened');after=texture('undo-child');before=dict(before);after=dict(after);before.pop('selected_layer');after.pop('selected_layer');assert before==after
    def difference(a_stage,b_stage):
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        return sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    unchanged={stage:difference('initial',stage) for stage in ['empty-folder','paint-above','grouped','collapsed','expanded','shown']}
    assert max(unchanged.values())<.0001,unchanged
    assert difference('initial','hidden')>.02 and difference('initial','opacity50')>.02
    result['folder_demo']={'pid':process.pid,'stages':stages,'nested_depth':2,'empty_folder_and_root_paint_insertion':True,
        'collapse_changes_only_visible_rows':True,'folder_visibility_and_channel_opacity_composite_children':True,
        'save_open_and_child_edit_undo':True,'nonpainting_folder_properties':True,'unchanged_viewport_differences':unchanged,'presentation_errors':errors}
if a.selection:
    demo=json.loads((a.output/'selection.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','disjoint','single','range','toggle-added','toggle-off','siblings','grouped','undo-group','redo-group','saved','reopened']
    first=captures['initial'];rows=[r['id'].strip('{}') for r in first['workspace']['layer_rows']];assert len(rows)==6
    expected=[[0],[0,3],[0],[0,1,2,3],[0,3],[0],[0,1]];errors={}
    def authored(c):
        value=json.loads(json.dumps(c['document']))
        for t in value['texture_sets']:t.pop('selected_layer')
        return value
    for index,(stage,c) in enumerate(captures.items()):
        ids=[id.strip('{}') for id in c['workspace']['selected_layers']]
        if index<7:
            assert ids==[rows[i] for i in expected[index]],(stage,ids,expected[index])
            assert authored(c)==authored(first)
            assert c['workspace']['roughness_visible']==(len(ids)==1)
            assert c['workspace']['roughness_editable']==(len(ids)==1)
        assert c['inspection']==2
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    original=first['document']['texture_sets'][1];grouped=captures['grouped']['document']['texture_sets'][1]
    group=next(l for l in grouped['layers'] if l['id']==grouped['selected_layer']);assert group['kind']=='folder'
    group_children=[l for l in grouped['layers'] if l['parent']==group['id']];assert [l['id'] for l in group_children]==[rows[1],rows[0]]
    assert not group_children[0]['visible'] and group_children[1]['visible']
    assert authored(captures['undo-group'])==authored(first)
    assert captures['redo-group']['document']==captures['grouped']['document']
    assert captures['saved']['document']==captures['reopened']['document']
    unchanged={}
    for stage in stages[1:]:
        left=Image.open(a.output/'initial-viewport.png').convert('RGB');right=Image.open(a.output/f'{stage}-viewport.png').convert('RGB')
        unchanged[stage]=sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    assert max(unchanged.values())<.0001,unchanged
    result['selection_demo']={'pid':process.pid,'stages':stages,'modifier':'Control adds/toggles; Shift selects visible range on Linux',
        'disjoint_root_and_child_selection':True,'range_includes_expanded_folder_and_child':True,'multi_properties_and_paint_disabled':True,
        'grouped_sibling_order_and_hidden_state_retained':True,'one_undo_restores_grouping':True,'save_open_exact':True,'unchanged_viewport_differences':unchanged,'presentation_errors':errors}
if a.blend or a.replace:
    demo=json.loads((a.output/('replace.json' if a.replace else 'blending.json')).read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures);first=captures[stages[0]];errors={}
    expected_stages=['normal','replace','base-context','undo','redo','saved','reopened','normal-restored'] if a.replace else ['normal','multiply','base-normal','base-screen','rough-screen','disabled','metal','restored','undo','redo','saved','reopened','color-standard']
    assert stages==expected_stages
    expected_modes=['Normal','Replace','Replace','Normal','Replace','Replace','Replace','Normal'] if a.replace else ['Normal','Multiply','Multiply','Multiply','Screen','Disable','Disable','Normal','Disable','Normal','Normal','Normal','Normal']
    expected_values=[.5,.15,.15,.15,.65,.3,.3,.5,.3,.5,.5,.5,.5]
    for index,((stage,c),mode) in enumerate(zip(captures.items(),expected_modes)):
        top=c['document']['texture_sets'][1]['layers'][-1];initial=first['document']['texture_sets'][1]['layers'][-1]
        assert top['blending'][2]==mode and top['visible'] and top['values']==initial['values']
        assert top['paint_hashes']==initial['paint_hashes'] and top['channel_opacity']==initial['channel_opacity']
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        evaluation=c['channel_evaluation']
        if a.replace:
            assert top['blending'][0]=='Normal' and c['inspection']==2
            alpha=captures['replace']['channel_evaluation']['coverage'];assert .499<alpha<=.5 # soft brush sampled at a texel center
            if mode=='Replace':assert evaluation['coverage']==alpha and evaluation['outside_coverage']==0 and abs(evaluation['roughness']-1)<1e-6
            else:assert evaluation['coverage']==1 and evaluation['outside_coverage']==1 and abs(evaluation['roughness']-(.3+.7*alpha))<1e-6
        else:
            assert top['blending'][0]==('Normal' if index<3 else 'Screen')
            if stage=='multiply':assert c['workspace']['selected_layer_id'].strip('{}')==c['document']['texture_sets'][1]['layers'][-2]['id']
            assert abs(evaluation['roughness']-expected_values[index])<1e-6,(stage,evaluation)
            assert all(abs(v-(.5 if index<3 else .75))<1e-6 for v in evaluation['color'])
            assert next(m for m in c['native']['materials'] if m['name']=='SphereSurface')['metallic']==1
            assert c['inspection']==(1 if stage=='metal' else 0 if stage=='color-standard' else 2)
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert captures['saved']['document']==captures['reopened']['document']
    if a.replace:
        assert captures['normal']['document']==captures['undo']['document']==captures['normal-restored']['document']
        assert captures['replace']['document']==captures['redo']['document']
    else:
        assert captures['disabled']['document']==captures['undo']['document']
        assert captures['restored']['document']==captures['redo']['document']==captures['color-standard']['document']
    def difference(a_stage,b_stage):
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        return sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)
    changed=difference('normal','replace' if a.replace else 'multiply');assert changed>.02,changed
    context_diff=difference('replace','base-context') if a.replace else difference('multiply','base-screen');assert context_diff<.0001,context_diff
    proof={'pid':process.pid,'stages':stages,'channel_modes_independent':True,'author_values_pixels_opacity_and_other_set_retained':True,
        'undo_redo_save_open_exact':True,'viewport_changed':changed,'unrelated_context_difference':context_diff,'presentation_errors':errors}
    if a.replace:proof['transparent_replace_retains_paint_coverage']=True
    else:
        point=captures['color-standard']['channel_evaluation']['uv_sample'];pixel=Image.open(a.output/'color-standard-viewport.png').convert('RGB').getpixel(tuple(point))
        assert all(abs(v-225)<=2 for v in pixel),pixel
        proof.update({'multiply':.15,'screen':.65,'disable_preserves_metallic':1,'linear_color_screen':.75,'standard_srgb_pixel':pixel,'encoded_space_screen_would_be':237})
    result['replace_demo' if a.replace else 'blending_demo']=proof
if a.masks:
    demo=json.loads((a.output/'masks.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','white','undo-white','black','paint3d','metal','paint2d','undo-stroke','redo-stroke','material-target','mask-target','bypass','restored','saved','reopened','edited','undo-edit']
    first=captures['initial'];errors={}
    def texture(stage):return captures[stage]['document']['texture_sets'][1]
    def mask(stage):return texture(stage)['layers'][-1]['mask']
    for stage,c in captures.items():
        t=texture(stage);top=t['layers'][-1];initial=texture('initial')['layers'][-1]
        assert top['values']==initial['values'] and top['visible'] and top['paint_hashes']==initial['paint_hashes'] and top['blending']==initial['blending']
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        assert c['mask_statistics']['coverage_channel_error']<1e-6
        assert c['workspace']['mask_grayscale_visible']==t['selected_mask']
        assert t['selected_mask']==(stage not in ['initial','undo-white','material-target'])
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert mask('white')['background']==1 and captures['white']['workspace']['mask_grayscale']==0
    assert mask('black')['background']==0 and captures['black']['workspace']['mask_grayscale']==1
    assert captures['black']['mask_statistics']['covered']==0
    assert 0<captures['paint3d']['mask_statistics']['covered']<65536
    assert mask('paint3d')==mask('metal')==mask('undo-stroke')
    assert mask('paint2d')==mask('redo-stroke')==mask('restored')==mask('saved')==mask('reopened')==mask('undo-edit')
    assert mask('paint2d')!=mask('paint3d') and mask('edited')!=mask('reopened')
    assert mask('bypass')['tiles']==mask('restored')['tiles'] and not mask('bypass')['enabled'] and mask('restored')['enabled']
    assert captures['saved']['document']==captures['reopened']['document']
    assert captures['initial']['document']==captures['undo-white']['document']
    def difference(a_stage,b_stage):
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        # MASK target labels are intentional overlays; compare pane interiors only.
        result=[]
        for pane in captures[a_stage]['view']['panes']:
            x,y,w,h=pane['rect'];box=(x+55,y+40,x+w-40,y+h-40)
            result.append(sum(ImageStat.Stat(ImageChops.difference(left.crop(box),right.crop(box))).mean)/(3*255))
        return max(result)
    assert difference('initial','white')<.003 and difference('initial','black')>.02
    assert difference('black','paint3d')>.001 and difference('restored','bypass')>.01
    assert difference('paint2d','restored')<.0001 and difference('reopened','undo-edit')<.0001
    result['mask_demo']={'pid':process.pid,'stages':stages,'white_black_opposite_brush_defaults':True,
        'three_dimensional_and_uv_mask_painting':True,'shared_color_metallic_coverage_error':max(c['mask_statistics']['coverage_channel_error'] for c in captures.values()),
        'material_and_mask_targets_distinct':True,'shift_bypass_retains_pixels':True,'undo_redo_save_open_exact':True,'presentation_errors':errors}
if a.bitmap_mask:
    demo=json.loads((a.output/'masks.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','cancelled','rejected','assigned','metal','undo','redo','saved','reopened','painted','undo-paint']
    def mask(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]['mask']
    assert captures['initial']['document']==captures['cancelled']['document']==captures['rejected']['document']==captures['undo']['document']
    assert mask('assigned')==mask('metal')==mask('redo')==mask('reopened')==mask('undo-paint')
    assert captures['saved']['document']==captures['reopened']['document'] and mask('painted')!=mask('assigned')
    source=Image.open(a.bitmap_mask).convert('RGB');assert source.size==(1024,1024)
    # ImBuf/UV origin is bottom-left; independent reference uses raw PNG samples.
    expected=sum(source.getpixel((x,1023-y))[0]/255 for y in range(0,1024,4) for x in range(0,1024,4))
    assert abs(captures['assigned']['mask_statistics']['sum']-expected)<.01,(captures['assigned']['mask_statistics'],expected)
    assert 0<expected<65536
    errors={}
    for stage,c in captures.items():
        assert c['mask_statistics']['coverage_channel_error']<1e-6
        assert c['document']['texture_sets'][0]==captures['initial']['document']['texture_sets'][0]
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['bitmap_mask_demo']={'pid':process.pid,'stages':stages,'raw_bitmap_reference_sum':expected,'native_sum':captures['assigned']['mask_statistics']['sum'],'cancel_and_reject_preserve_document':True,'undo_redo_save_open_exact':True,'imported_pixels_remain_paintable':True,'presentation_errors':errors}
if a.isolate_mask:
    demo=json.loads((a.output/'masks.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','painted','isolated','isolated-again','exposure','standard','edited','undo','redo','bypassed','enabled','material','saved','reopened','isolated-open']
    def mask(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]['mask']
    assert captures['painted']['document']==captures['isolated']['document']==captures['isolated-again']['document']==captures['exposure']['document']==captures['standard']['document']==captures['undo']['document']
    assert captures['edited']['document']==captures['redo']['document']==captures['enabled']['document']==captures['material']['document']==captures['saved']['document']==captures['reopened']['document']==captures['isolated-open']['document']
    assert mask('edited')!=mask('isolated') and mask('bypassed')['tiles']==mask('edited')['tiles'] and not mask('bypassed')['enabled']
    images={};errors={}
    for stage,c in captures.items():
        assert c['inspection']==(-1 if stage in ['initial','painted','material','saved','reopened'] else 5)
        assert c['mask_statistics']['coverage_channel_error']<1e-6
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');images[stage]=image
        window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def delta(a_stage,b_stage):
        diff=ImageChops.difference(images[a_stage],images[b_stage]);return sum(ImageStat.Stat(diff).mean)/(3*255)
    for before,after in [('isolated','isolated-again'),('isolated','exposure'),('isolated','standard'),('isolated','undo'),('edited','redo'),('edited','bypassed'),('edited','enabled'),('edited','isolated-open')]:
        assert captures[before]['mask_image_hash']==captures[after]['mask_image_hash'],(before,after,'raw mask texels')
        pane=next(p for p in captures[before]['view']['panes'] if p['view']=='2D');x,y,w,h=map(round,pane['rect']);box=(x,y,x+w,y+h)
        assert ImageChops.difference(images[before].crop(box),images[after].crop(box)).getbbox() is None,(before,after,'UV mask pixels')
        # EEVEE temporal sampling may vary slightly in 3D after an image rebind;
        # raw float mask texels and the unlit UV framebuffer stay exact.
        assert delta(before,after)<.0002,(before,after,delta(before,after))
    assert delta('painted','isolated')>.01 and delta('isolated','edited')>.001 and delta('enabled','material')>.01
    result['mask_isolation_demo']={'pid':process.pid,'stages':stages,'raw_mask_unaffected_by_environment_or_display_transform':True,'repeated_alt_click_retains_isolation':True,'painting_and_undo_live_in_isolation':True,'bypassed_mask_still_inspectable':True,'material_return_retains_selection_and_pixels':True,'save_open_exact':True,'presentation_errors':errors}
if a.invert_mask:
    demo=json.loads((a.output/'masks.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','painted','inverted','undo','redo','restored','saved','reopened','inverted-open']
    def top(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]
    for stage,c in captures.items():
        assert c['mask_statistics']['coverage_channel_error']<1e-6
        if stage=='initial':continue
        assert top(stage)['mask']['background']==(1 if stage in ['inverted','redo','inverted-open'] else 0)
        assert top(stage)['mask']['tiles']==top('painted')['mask']['tiles']
        assert top(stage)['values']==top('painted')['values'] and top(stage)['blending']==top('painted')['blending']
        assert top(stage)['paint_hashes']==top('painted')['paint_hashes'] and c['inspection']==5
    for a_stage,b_stage in [('painted','undo'),('inverted','redo'),('painted','restored'),('saved','reopened'),('inverted','inverted-open')]:
        assert captures[a_stage]['document']==captures[b_stage]['document']
        left=Image.open(a.output/f'{a_stage}-viewport.png').convert('RGB');right=Image.open(a.output/f'{b_stage}-viewport.png').convert('RGB')
        assert sum(ImageStat.Stat(ImageChops.difference(left,right)).mean)/(3*255)<.0001
    assert captures['inverted']['mask_statistics']['sum']>captures['painted']['mask_statistics']['sum']+40000
    result['mask_background_demo']={'pid':process.pid,'stages':stages,'authored_pixels_retained':True,'parent_channels_and_blends_retained':True,'undo_redo_save_open_exact':True,'background_inverted_without_negating_painted_values':True}
if a.eraser:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['empty','painted','eraser','erased3d','undo3d','erased2d','undo2d','redo2d','rough-disabled','disabled-stroke','cancelled','paint-tool','eraser-restored','saved','reopened','erased-reopened']
    def top(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]
    def sums(stage):return captures[stage]['erase_statistics']['coverage_sums']
    errors={}
    for stage,c in captures.items():
        assert c['erase_statistics']['underlying_fill_error']<1e-6
        assert c['document']['texture_sets'][0]==captures['empty']['document']['texture_sets'][0]
        assert len(c['document']['texture_sets'][1]['layers'])==3
        active=stage not in ['empty','painted','paint-tool','reopened']
        assert c['workspace']['eraser_active']==active and c['workspace']['roughness_visible']==(not active)
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert captures['eraser']['workspace']['brush_size']==10 and captures['eraser']['workspace']['brush_flow']==100 and captures['eraser']['workspace']['stroke_opacity']==100
    assert captures['paint-tool']['workspace']['brush_size']==15 and captures['eraser-restored']['workspace']['brush_size']==10
    assert captures['paint-tool']['workspace']['brush_channels']==[True]*5
    assert captures['eraser-restored']['workspace']['brush_channels']==[True,True,False,True,True]
    for erased in ['erased3d','erased2d']:
        assert all(0<a<b-1 for a,b in zip(sums(erased),sums('painted'))),(erased,sums(erased),sums('painted'))
    for a_stage,b_stage in [('painted','eraser'),('painted','undo3d'),('painted','undo2d'),('erased2d','redo2d'),('disabled-stroke','cancelled'),('saved','reopened')]:assert captures[a_stage]['document']==captures[b_stage]['document']
    assert top('disabled-stroke')['paint_hashes'][2]==top('redo2d')['paint_hashes'][2]
    assert all(top('disabled-stroke')['paint_hashes'][c]!=top('redo2d')['paint_hashes'][c] for c in [0,1,3,4])
    assert sums('erased-reopened')[2]<sums('reopened')[2]-1
    result['eraser_demo']={'pid':process.pid,'stages':stages,'removes_coverage_revealing_underlying_fill':True,'three_dimensional_and_uv_erasing':True,'whole_stroke_undo_redo':True,'independent_channel_exclusion':True,'cancel_preserves_document':True,'independent_tool_settings':True,'save_open_and_continued_erasing':True,'presentation_errors':errors}
if a.alpha:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['shape','plus-selected','plus3d','inverted','inverted2d','cleared','uniform','bitmap-selected','bitmap2d','undo','redo','eraser-alpha','erased','saved','reopened']
    def top(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]
    def sample(stage,index):return captures[stage]['alpha_samples'][index]
    for before,after in [('shape','plus-selected'),('plus3d','inverted'),('inverted2d','cleared'),('uniform','bitmap-selected'),('bitmap-selected','undo'),('bitmap2d','redo'),('redo','eraser-alpha'),('saved','reopened')]:assert captures[before]['document']==captures[after]['document'],(before,after)
    assert captures['plus-selected']['workspace']['alpha_name']=='Sign Plus' and not captures['plus-selected']['workspace']['alpha_inverted']
    assert top('plus3d')['paint_hashes']!=top('plus-selected')['paint_hashes']
    assert captures['inverted']['workspace']['alpha_inverted']
    assert sample('inverted2d',0)==0 and sample('inverted2d',1)==1
    assert captures['cleared']['workspace']['alpha_name']=='uniform color' and captures['cleared']['workspace']['alpha_uniform']==1
    assert abs(sample('uniform',2)-.25)<1e-6
    assert captures['bitmap-selected']['workspace']['alpha_name']==a.alpha.stem
    assert not captures['bitmap-selected']['workspace']['alpha_invert_visible'] and not captures['bitmap-selected']['workspace']['alpha_hardness_visible']
    source=Image.open(a.alpha).convert('RGB');width,height=source.size
    def expected(uv):
        x,y=[int(v*1024) for v in uv];u=((x+.5)/1024-.25)/.4+.5;v=((y+.5)/1024-.2)/.4+.5
        sx=u*width-.5;sy=v*height-.5;ix=math.floor(sx);iy=math.floor(sy);fx=sx-ix;fy=sy-iy
        def at(x,y):return source.getpixel((min(width-1,max(0,x)),height-1-min(height-1,max(0,y))))[0]/255
        return ((1-fx)*at(ix,iy)+fx*at(ix+1,iy))*(1-fy)+((1-fx)*at(ix,iy+1)+fx*at(ix+1,iy+1))*fy
    for index,uv in [(3,(.25,.2)),(4,(.2,.16))]:assert abs(sample('bitmap2d',index)-expected(uv))<1e-5,(index,sample('bitmap2d',index),expected(uv))
    assert sample('erased',3)==0 and sample('erased',4)==sample('bitmap2d',4)
    errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][0]==captures['shape']['document']['texture_sets'][0]
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['alpha_demo']={'pid':process.pid,'stages':stages,'shelf_double_click_selects_plus':True,'three_dimensional_plus_and_uv_inversion':True,'clear_selects_uniform_one':True,'bitmap_coverage_matches_raw_source':True,'existing_pixels_unchanged_by_settings':True,'eraser_uses_selected_alpha':True,'undo_redo_save_open_exact':True,'resources_are_session_scoped':True,'presentation_errors':errors}
if a.padding:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['3d','2d','undo','redo','normal-2d','normal-3d','rough-3d','saved','reopened','2d-after-open','other-set','returned']
    def sample(stage,x,channel):return next(s for s in captures[stage]['padding_samples'] if s['x']==x)['channels'][channel]
    def close(a,b):return all(abs(x-y)<1e-5 for x,y in zip(a,b))
    assert sample('3d',461,2)[0]>.7 and sample('2d',461,2)[0]<.3,(sample('3d',461,2),sample('2d',461,2))
    assert sample('3d',461,0)[0]>.7 and sample('2d',461,0)[0]<.3
    assert close(sample('normal-2d',461,3),sample('normal-3d',461,3))
    for stage,c in captures.items():
        assert close(sample(stage,459,2),[.2,.2,.2,1]) and close(sample(stage,564,2),[.8,.8,.8,1])
        assert close(sample(stage,461,3),[.5,.5,1,1])
        assert c['document']['texture_sets'][0]==captures['3d']['document']['texture_sets'][0]
        left=c['document']['texture_sets'][1];before=captures['3d']['document']['texture_sets'][1]
        assert left['layers']==before['layers'] and left['selected_layer']==before['selected_layer']
        mode=stage not in ['2d','redo','normal-2d','2d-after-open','other-set','returned']
        assert left['padding_3d']==mode
        assert c['padding_control']==('3D Space Neighbor' if mode or stage=='other-set' else '2D Space Neighbor')
    for before,after in [('3d','undo'),('2d','redo'),('saved','reopened'),('2d-after-open','returned')]:assert captures[before]['document']==captures[after]['document']
    errors={}
    for stage,c in captures.items():
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    def uv_image(stage):
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');pane=next(p for p in captures[stage]['view']['panes'] if p['view']=='2D');x,y,w,h=pane['rect'];return image.crop((x+50,y+40,x+w-30,y+h-35))
    # Normal data uses 2D extension in either mode; Roughness has an intentional seam discontinuity.
    assert sum(ImageStat.Stat(ImageChops.difference(uv_image('normal-2d'),uv_image('normal-3d'))).mean)<.01
    assert sum(ImageStat.Stat(ImageChops.difference(uv_image('3d'),uv_image('2d'))).mean)>.1
    result['padding_demo']={'pid':process.pid,'stages':stages,'native_algorithms':['RE_bake_margin/R_BAKE_EXTEND','RE_bake_margin/R_BAKE_ADJACENT_FACES'],'margin_pixels':2,'seam_roughness_2d':sample('2d',461,2),'seam_roughness_3d':sample('3d',461,2),'normal_always_2d':True,'authored_layers_and_set_isolation_retained':True,'undo_redo_save_open_exact':True,'presentation_errors':errors}
if a.projection:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['empty','wrap','camera','planar','uv','large-wrap','large-planar','cull10','rejected','off','admitted','undo','redo','uv-controls','wrap-restored','saved','reopened']
    def top(stage):return captures[stage]['document']['texture_sets'][1]['layers'][-1]
    def stats(stage):return captures[stage]['projection_statistics']
    hashes=[top(stage)['paint_hashes'][2] for stage in ['wrap','camera','planar','uv']];assert len(set(hashes))==4,hashes
    assert all(stats(s)['covered']>20 for s in ['wrap','camera','planar','uv','large-wrap','large-planar','admitted'])
    assert top('large-wrap')['paint_hashes']!=top('large-planar')['paint_hashes'] and abs(stats('large-wrap')['sum']-stats('large-planar')['sum'])>1
    assert stats('uv')['uv_samples']==[1,1,0],stats('uv')
    assert captures['empty']['document']==captures['cull10']['document']==captures['rejected']['document']==captures['off']['document']==captures['undo']['document']
    assert not captures['off']['workspace']['backface_culling'] and captures['off']['workspace']['culling_angle']==10 and not captures['off']['workspace']['culling_angle_enabled']
    assert stats('rejected')['covered']==0
    for before,after in [('admitted','redo'),('redo','uv-controls'),('uv-controls','wrap-restored'),('saved','reopened')]:assert captures[before]['document']==captures[after]['document']
    for stage in ['uv','uv-controls']:
        assert captures[stage]['workspace']['brush_alignment']=='UV' and not captures[stage]['workspace']['culling_control_enabled'] and not captures[stage]['workspace']['culling_angle_enabled']
    assert captures['wrap-restored']['workspace']['culling_angle']==90 and captures['wrap-restored']['workspace']['backface_culling']
    errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][0]==captures['empty']['document']['texture_sets'][0]
        assert c['document']['texture_sets'][1]['layers'][:-1]==captures['empty']['document']['texture_sets'][1]['layers'][:-1]
        if stage!='reopened':assert c['view']['scene_view_matrix']==captures['empty']['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['projection_demo']={'pid':process.pid,'stages':stages,'four_distinct_native_footprints':True,'wrap_geodesic_unfolding_planar_depth_fade':True,'large_wrap_coverage_sum':stats('large-wrap')['sum'],'large_planar_coverage_sum':stats('large-planar')['sum'],'uv_axis_aligned':True,'ten_degree_filter_rejects_steep_visible_surface':True,'disabled_culling_admits_same_stamp':True,'retained_disabled_controls':True,'settings_do_not_reproject_authored_pixels':True,'undo_redo_save_open_exact':True,'presentation_errors':errors}
if a.symmetry:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['empty','enabled','manipulator','moved','configured','pair','undo','redo','off','single','undo-single','radial','radial-painted','undo-radial','redo-radial','saved','reopened','after-open','undo-open']
    first=captures['empty']
    assert not first['symmetry']['enabled'] and captures['enabled']['symmetry']['enabled']
    for stage in ['enabled','manipulator','moved','configured','undo']:assert captures[stage]['document']==first['document']
    assert captures['manipulator']['symmetry']['manipulator'] and not captures['configured']['symmetry']['manipulator']
    assert abs(captures['moved']['symmetry']['offset']-captures['manipulator']['symmetry']['offset'])>.001
    assert captures['pair']['symmetry_copies']==2 and captures['single']['symmetry_copies']==1
    assert captures['radial-painted']['symmetry_copies']==4 and captures['after-open']['symmetry_copies']==4
    assert captures['pair']['symmetry_pixels']['sum']>0 and captures['single']['symmetry_pixels']['sum']>captures['pair']['symmetry_pixels']['sum']
    for before,after in [('pair','redo'),('pair','off'),('pair','undo-single'),('pair','radial'),('pair','undo-radial'),('radial-painted','redo-radial'),('saved','reopened'),('saved','undo-open')]:assert captures[before]['document']==captures[after]['document'],(before,after)
    assert captures['undo']['symmetry']==captures['pair']['symmetry']
    assert captures['undo-radial']['symmetry']==captures['radial-painted']['symmetry']
    assert captures['saved']['symmetry']==captures['reopened']['symmetry']==captures['after-open']['symmetry']
    assert captures['radial-painted']['symmetry_pixels']['sum']>captures['pair']['symmetry_pixels']['sum']
    assert captures['after-open']['document']!=captures['reopened']['document']
    errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        assert len(c['document']['texture_sets'][1]['layers'])==3
        if stage not in ['reopened','after-open','undo-open']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['symmetry_demo']={'pid':process.pid,'stages':stages,'mirror_pair_and_single_without_symmetry':True,'radial_four_copies':True,'plane_drag_preserves_camera_and_layers':True,'whole_stroke_undo_preserves_symmetry':True,'settings_persist_in_native_blend':True,'save_open_and_continued_symmetric_painting':True,'world_origin_default_without_selected_set_recentering':True,'presentation_errors':errors}
if a.straight:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['empty','anchor','first','second','undo','redo','anchor2','snapped','undo-snap','undo-anchor','other-set','returned','reset-dab','undo-reset','saved','reopened','after-open','undo-open']
    def sample(stage,index):return captures[stage]['line_samples'][index]
    assert sample('anchor',0)>.5 and sample('anchor',1)==0
    assert sample('first',1)>.5 and sample('first',2)==0
    assert sample('second',1)>.5 and sample('second',2)>.5
    assert sample('snapped',3)>.5 and sample('snapped',4)>.5 and sample('snapped',5)==0
    end=captures['snapped']['line_anchor'];assert abs(end[0]-.55)<1e-6 and abs(end[1]-(.9-.35/math.sqrt(3)))<1e-6,end
    for before,after in [('first','undo'),('second','redo'),('anchor2','undo-snap'),('second','undo-anchor'),('second','undo-reset'),('saved','reopened'),('saved','undo-open')]:assert captures[before]['document']==captures[after]['document'],(before,after)
    for stage in ['returned','reopened']:assert captures[stage]['line_anchor'] is None
    assert sample('reset-dab',6)>.5 and sample('after-open',7)>.5
    first=captures['empty'];errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        assert len(c['document']['texture_sets'][1]['layers'])==3
        if stage not in ['reopened','after-open','undo-open']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['straight_demo']={'pid':process.pid,'stages':stages,'uv_continuous_segments_and_corner':True,'angle_snap_30_degrees':True,'one_undo_per_segment':True,'target_switch_and_open_reset_anchor':True,'save_open_preserves_editable_pixels':True,'presentation_errors':errors}
if a.lazy:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['defaults','enabled8','distance80','dab80','undo80','disabled80','line-off','undo-off','enabled8-again','line8','undo8','disabled8']
    first=captures['defaults']
    for stage in ['defaults','disabled80','line-off','undo-off','disabled8']:assert not captures[stage]['workspace']['lazy_mouse'] and not captures[stage]['workspace']['lazy_distance_enabled']
    for stage in ['enabled8','distance80','dab80','undo80','enabled8-again','line8','undo8']:assert captures[stage]['workspace']['lazy_mouse'] and captures[stage]['workspace']['lazy_distance_enabled']
    for stage in ['distance80','dab80','undo80','disabled80','line-off','undo-off']:assert captures[stage]['workspace']['lazy_distance']==80
    for stage in ['defaults','enabled8','enabled8-again','line8','undo8','disabled8']:assert captures[stage]['workspace']['lazy_distance']==8
    assert captures['dab80']['lazy_samples'][0]>.5 and captures['dab80']['lazy_samples'][1:]==[0,0,0]
    assert min(captures['line-off']['lazy_samples'])>.5
    assert min(captures['line8']['lazy_samples'][:3])>.5 and captures['line8']['lazy_samples'][3]==0
    assert abs(captures['line8']['lazy_anchor'][0]-(.65-.08*first['view']['uv_vertical_extent']))<1e-6
    assert abs(captures['dab80']['lazy_anchor'][0]-.2)<1e-6 and abs(captures['line-off']['lazy_anchor'][0]-.65)<1e-6
    errors={}
    for stage,c in captures.items():
        if stage not in ['dab80','line-off','line8']:assert c['document']==first['document'],stage
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['lazy_demo']={'pid':process.pid,'stages':stages,'d_toggle_retains_distance_and_pixels':True,'large_distance_start_dab_only':True,'disabled_full_line':True,'small_distance_delayed_endpoint_without_release_catch_up':True,'whole_stroke_undo_retains_settings':True,'presentation_errors':errors}
if a.picker:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','enabled','bare','enabled-stripe','stripe','partial','enabled3d','sampled3d','rough-disabled','masked-channel','eraser','eraser-sampled','empty','cancelled']
    first=captures['initial'];errors={}
    for stage in ['enabled','enabled-stripe','enabled3d']:assert captures[stage]['picker_active']
    for stage in ['bare','stripe','partial','sampled3d','masked-channel','eraser-sampled','empty','cancelled']:assert not captures[stage]['picker_active']
    for stage,roughness in [('bare',.2),('stripe',1),('partial',.6)]:assert abs(captures[stage]['picker_brush'][5]-roughness)<1e-6,(stage,captures[stage]['picker_brush'])
    for stage in ['bare','stripe','partial','sampled3d','eraser-sampled']:assert max(abs(a-b) for a,b in zip(captures[stage]['picker_brush'],captures[stage]['picker_expected']))<1e-6,stage
    assert abs(captures['masked-channel']['picker_brush'][5]-.07)<1e-6
    assert captures['eraser']['workspace']['eraser_active'] and captures['eraser-sampled']['workspace']['eraser_active']
    assert captures['empty']['picker_brush']==captures['cancelled']['picker_brush']==captures['eraser-sampled']['picker_brush']
    assert captures['bare']['picker_brush']!=first['picker_brush']
    for stage,c in captures.items():
        assert c['document']==first['document'],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['picker_demo']={'pid':process.pid,'stages':stages,'known_value_transfer_uv_and_native_3d':True,'partial_stroke_samples_composite':True,'brush_disabled_channel_preserved':True,'returns_to_paint_or_eraser_without_pixel_edits':True,'background_and_escape_keep_values':True,'presentation_errors':errors}
if a.polygon:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','triangle-mode','triangle','polygon3d','island','mesh','black-island','undo-black','redo-black','rectangle','undo-rectangle','redo-rectangle','saved','reopened','material','painted','undo-paint']
    def stats(stage):return captures[stage]['polygon']
    assert stats('initial')['active'] and stats('initial')['mode']==4 and stats('initial')['grayscale']==1
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
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['polygon_demo']={'pid':process.pid,'stages':stages,'four_native_geometry_fill_modes':True,'black_island_overwrites_white':True,'rectangle_is_one_undo_transaction':True,'controls_synchronized_picker_disabled':True,'save_open_and_continued_mask_painting':True,'other_texture_set_unchanged':True,'presentation_errors':errors}
if a.fill_uv:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['uniform','assigned','scale4','rotation30','offset','collapsed','expanded','undo-offset','redo-offset','saved','reopened','edited','undo-edit','other-set','returned','cleared','undo-clear']
    def stats(stage):return captures[stage]['fill_uv']
    for stage in ['uniform','assigned']:assert stats(stage)['scale_u']==stats(stage)['scale_v']==1
    assert stats('scale4')['scale_u']==stats('scale4')['scale_v']==4 and not stats('scale4')['scale_v_enabled']
    assert stats('rotation30')['rotation']==30 and abs(stats('offset')['offset_u']-.1)<1e-6
    assert not stats('collapsed')['disclosure'] and stats('expanded')['disclosure']
    assert len(set(stats(stage)['hashes'][0] for stage in ['assigned','scale4','rotation30','offset']))==4
    assert len(set(tuple(stats(stage)['hashes'][1:]) for stage in ['assigned','scale4','rotation30','offset']))==1
    assert abs(stats('scale4')['frame'][1][0]-stats('scale4')['frame'][0][0]-.25)<1e-6
    for before,after in [('offset','collapsed'),('offset','expanded'),('rotation30','undo-offset'),('offset','redo-offset'),('saved','reopened'),('saved','undo-edit'),('saved','returned'),('saved','undo-clear')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        assert stats(before)['hashes']==stats(after)['hashes'],(before,after)
    first=captures['uniform'];errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1]
        assert len(c['document']['texture_sets'][0]['layers'])==3
        for layer in c['document']['texture_sets'][0]['layers']:assert layer['paint_pages']==[0]*5
        if stage!='other-set':assert stats(stage)['panel_visible'] and not stats(stage)['paint_enabled']
        if stage not in ['reopened','edited','undo-edit','other-set','returned','cleared','undo-clear']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['fill_uv_demo']={'pid':process.pid,'stages':stages,'live_procedural_base_color':True,'linked_scale_rotation_offset_change_native_images':True,'other_channels_and_set_unchanged':True,'quarter_size_uv_frame':True,'collapsed_controls_keep_projection':True,'undo_redo_and_editable_save_open':True,'no_authored_paint_pages':True,'presentation_errors':errors}
if a.triplanar:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['uv','tri','expanded','offsetX','rotate','translate','scale','hardness','undo-hardness','redo-hardness','saved','reopened','edited','undo-edit','uv-restored','tri-returned','hidden']
    def stats(stage):return captures[stage]['triplanar']
    def hashes(stage):return captures[stage]['fill_uv']['hashes']
    assert stats('tri')['offset3']==stats('tri')['rotation3']==[0]*3 and stats('tri')['scale3']==[1]*3
    assert stats('tri')['hardness']==.5 and not stats('tri')['expanded'] and stats('expanded')['expanded']
    assert abs(stats('tri')['uv_offset']-.1)<1e-6 and not stats('tri')['uv_offset_visible'] and stats('uv-restored')['uv_offset_visible']
    assert stats('offsetX')['offset3']==[.2,0,0]
    assert hashes('uv')[0]!=hashes('tri')[0]!=hashes('offsetX')[0]
    for stage,gizmo in [('rotate',1),('translate',0),('scale',2)]:assert stats(stage)['gizmo']==gizmo and hashes(stage)==hashes('offsetX')
    for before,after in [('scale','undo-hardness'),('hardness','redo-hardness'),('saved','reopened'),('saved','undo-edit'),('saved','tri-returned')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        assert hashes(before)==hashes(after),(before,after)
    assert hashes('edited')[0]!=hashes('saved')[0] and hashes('uv-restored')==hashes('uv')
    assert not stats('hidden')['manipulator'] and hashes('hidden')==hashes('saved')
    first=captures['uv'];errors={}
    for stage,c in captures.items():
        t=c['triplanar'];assert c['fill_uv']['scale_u']==c['fill_uv']['scale_v']==4 and c['fill_uv']['rotation']==30
        if t['mode']==1:assert t['geometry_valid']>1000 and t['maximum_normal_error']<1e-5
        assert hashes(stage)[1:]==hashes('uv')[1:]
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1]
        for layer in c['document']['texture_sets'][0]['layers']:assert layer['paint_pages']==[0]*5
        if stage not in ['reopened','edited','undo-edit','uv-restored','tri-returned','hidden']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['triplanar_demo']={'pid':process.pid,'stages':stages,'native_blender_uv_bake_position_normal_atlas':True,'three_normal_weighted_projections':True,'separate_uv_and_3d_offsets':True,'retained_uv_scale_rotation':True,'gizmo_modes_do_not_change_material':True,'offset_rotation_and_hardness_editable_after_save_open':True,'other_channels_set_and_authored_pages_unchanged':True,'presentation_errors':errors}
if a.resources:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','project-import','project','all','session-import','user-import','user','preferences','saved','reopened','project-open','painted','undo-paint','new-project','empty-project','session','user-after-new']
    def resources(stage,scope):return [item for item in captures[stage]['resources']['catalog'] if item['scope']==scope]
    def visible(stage):return captures[stage]['resources']['visible']
    assert visible('project')==visible('project-open')==['SphereSurface_Roughness']
    assert visible('user')==visible('user-after-new')==['User Witness'] and visible('session')==['Session Witness']
    assert not visible('empty-project') and not resources('new-project','project')
    assert captures['all']['resources']['search']=='' and captures['all']['resources']['scope']=='All libraries'
    assert resources('project-import','project')==resources('reopened','project')
    assert resources('session-import','session')==resources('new-project','session')
    assert resources('user-import','user')==resources('new-project','user')
    assert len(captures['user-after-new']['resources']['disk_user'])==1
    user=resources('user-import','user')[0];assert user['sha256']==captures['user-after-new']['resources']['disk_user'][0]['sha256']
    assert not resource_source.exists() and captures['project-open']['workspace']['alpha_name']=='SphereSurface_Roughness'
    assert captures['painted']['document']!=captures['project-open']['document'] and captures['undo-paint']['document']==captures['project-open']['document']
    first=captures['initial'];errors={}
    for stage,c in captures.items():
        if stages.index(stage)<=12 and stage!='painted':assert c['document']==first['document'],stage
        if stages.index(stage)<9:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['resources_demo']={'pid':process.pid,'stages':stages,'separate_builtin_session_project_user_libraries':True,'imports_do_not_edit_layers':True,'project_source_packed_and_usable_after_original_copy_removed':True,'project_scope_cleared_on_new_session_user_retained':True,'user_library_reloaded_byte_exact':True,'resource_application_stroke_and_undo':True,'presentation_errors':errors}
if a.asset_search:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','imported','combined','empty','builtin','exact','quoted','path','reset','category-reset','saved','reopened']
    def visible(stage):return captures[stage]['resources']['visible']
    assert visible('combined')==visible('path')==visible('reopened')==['SphereSurface_Roughness']
    assert visible('empty')==[] and visible('builtin')==['Atom','Atom Simple'] and visible('exact')==['Atom'] and visible('quoted')==['Sign Plus']
    assert len(visible('reset'))==len(captures['initial']['resources']['catalog'])+1 and not captures['reset']['resources']['reset_enabled'] and captures['reset']['resources']['search']==''
    assert visible('category-reset')==['Atom','Atom Simple','Shape','Sign Plus','SphereSurface_Roughness'] and captures['category-reset']['resources']['search']==''
    first=captures['initial'];errors={}
    for stage,c in captures.items():
        assert c['document']==first['document'],stage
        if stage!='reopened':assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['asset_search_demo']={'pid':process.pid,'stages':stages,'conjunctive_scope_usage_name_path_filters':True,'incompatible_constraints_empty_without_fallback':True,'case_insensitive_exact_equality_and_quoted_names':True,'reset_and_category_replace_context':True,'queries_do_not_change_document_or_camera':True,'presentation_errors':errors}
if a.asset_categories:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','alphas','procedurals','brushes','hard-preset','soft-preset','materials','material-applied','undo-material','texture-import','textures','environments','particles-empty','all','saved','reopened']
    def visible(stage):return captures[stage]['resources']['visible']
    assert visible('alphas')==['Atom','Atom Simple','Shape','Sign Plus'] and visible('procedurals')==['Atom','Atom Simple']
    assert visible('brushes')==['Basic Hard','Basic Soft'] and {'Matte Gray','Polished Metal'}.issubset(visible('materials'))
    assert captures['hard-preset']['asset_categories']['brush_hardness']==1 and captures['soft-preset']['asset_categories']['brush_hardness']==0
    assert visible('textures')==['Texture Witness'] and visible('environments')==['TT Environment'] and visible('particles-empty')==[]
    assert captures['procedurals']['resources']['search']=='' and len(visible('all'))==len(captures['initial']['resources']['catalog'])+1
    first=captures['initial'];errors={}
    assert captures['material-applied']['document']!=first['document']
    assert captures['material-applied']['document']['texture_sets'][1]['layers'][-1]['name']=='Matte Gray'
    for stage,c in captures.items():
        if stage!='material-applied':assert c['document']==first['document'],stage
        assert c['asset_categories']['order']==['All','Project','Alphas','Grunges','Procedurals','Textures','Hard Surfaces','Skin','Filters','Brushes','Particles','Tools','Materials','Smart materials','Smart masks','Environments','Color profiles','Flattened layers']
        if stage!='reopened':assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['asset_categories_demo']={'pid':process.pid,'stages':stages,'seventeen_ordered_categories':True,'six_populated_core_types_filtered_separately':True,'empty_unsupported_groups_do_not_fallback':True,'hard_soft_brush_presets_apply_without_document_edits':True,'material_preset_adds_one_editable_fill_and_undo_restores_stack':True,'category_changes_preserve_document_and_camera':True,'presentation_errors':errors}
if a.editable_path:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'selected', 'moved', 'undo-move', 'redo-move', 'cancelled', 'smooth', 'custom', 'handle', 'broken', 'scaled', 'inserted', 'pressure', 'reversed', 'closed', 'deleted', 'undo-delete', 'display', 'saved', 'reopened', 'reselected', 'moved-open', 'undo-open', 'sphere-open', 'sphere-selected', 'sphere-custom', 'sphere-moved', 'sphere-undo']
    assert list(captures)==stages
    def data(stage):return captures[stage]['curve']
    def definition(stage):return data(stage)['definition']
    def point(stage,index):return definition(stage)['points'][index]
    def length(vector):return sum(v*v for v in vector)**.5
    assert data('setup')['schema']==captures['setup']['document']['schema']>=45 and data('setup')['path_count']==1 and len(definition('setup')['points'])==3
    assert data('selected')['selected_vertex']==1 and data('selected')['controls_visible'] and not data('selected')['drag_active']
    for before,after in [('selected','moved'),('reselected','moved-open'),('sphere-custom','sphere-moved')]:
        assert data(before)['hashes'][0]!=data(after)['hashes'][0] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
        assert data(after)['undo_count']==data(before)['undo_count']+1 and not data(after)['drag_active'],(before,after)
    for before,after in [('setup','selected'),('setup','undo-move'),('moved','redo-move'),('redo-move','cancelled'),('closed','undo-delete'),('undo-delete','display'),('display','saved'),('saved','reopened'),('reopened','reselected'),('reopened','undo-open'),('sphere-custom','sphere-undo')]:
        for key in ['definition','hashes','native_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
    assert data('cancelled')['undo_count']==data('redo-move')['undo_count'] and data('cancelled')['redo_count']==data('redo-move')['redo_count']
    assert point('smooth',1)['curve']['smooth'] and not point('smooth',1)['curve']['custom']
    assert point('custom',1)['curve']['custom'] and not point('custom',1)['curve']['broken']
    assert point('handle',1)['curve']['out']!=point('custom',1)['curve']['out']
    assert point('broken',1)['curve']['broken'] and point('broken',1)['curve']['out']==point('handle',1)['curve']['out']
    before,after=point('broken',1)['curve'],point('scaled',1)['curve']
    ratios=[length(after[key])/length(before[key]) for key in ['in','out']];assert abs(ratios[0]-ratios[1])<1e-3 and all(abs(r-1.3)<1e-3 for r in ratios),ratios
    assert len(definition('inserted')['points'])==4 and data('inserted')['selected_vertex']==1
    assert abs(point('pressure',1)['pressure']-.5)<1e-6 and data('pressure')['pressure_control']==50 and data('pressure')['hashes'][0]!=data('inserted')['hashes'][0]
    assert [p['position'] for p in definition('reversed')['points']]==[p['position'] for p in reversed(definition('pressure')['points'])]
    assert definition('closed')['closed'] and len(definition('deleted')['points'])==3
    assert data('display')['display']['handle']==10 and data('display')['display']['width']==2 and data('display')['display']['normals']
    assert captures['display']['document']==captures['undo-delete']['document']
    assert not data('reopened')['active'] and definition('reopened')['version']>=2
    assert point('sphere-moved',1)['position']!=point('sphere-custom',1)['position'] and point('sphere-moved',1)['normal']!=point('sphere-custom',1)['normal']
    for stage in stages[1:]:
        baseline='setup' if stages.index(stage)<24 else 'sphere-open'
        assert data(stage)['old_mark']==data(baseline)['old_mark'] and data(stage)['path_count']==1,stage
        target=next(t for t in captures[stage]['document']['texture_sets'] if t['name']==data(stage)['target']);original=next(t for t in captures[baseline]['document']['texture_sets'] if t['name']==data(baseline)['target']);assert len(target['layers'])==len(original['layers']),stage
        other='Z_OtherSurface' if baseline=='setup' else 'CubeSurface'
        assert next(t for t in captures[stage]['document']['texture_sets'] if t['name']==other)==next(t for t in captures[baseline]['document']['texture_sets'] if t['name']==other),stage
        for vertex in definition(stage)['points']:
            if vertex['curve']['custom']:
                for key in ['in','out']:assert abs(sum(a*b for a,b in zip(vertex['normal'],vertex['curve'][key])))<1e-4,stage
    result['editable_path_demo']={'pid':process.pid,'stages':stages,'single_gesture_vertex_and_tangent_undo_and_escape_cancel':True,'surface_point_drag_reprojects_handles_on_new_normal_plane':True,'smooth_corner_custom_alt_break_ctrl_scale':ratios,'curve_insertion_pressure_reverse_closed_and_delete_undo':True,'display_settings_change_overlay_only':True,'curve_save_open_further_edit':True,'document_schema':data('setup')['schema'],'plane_and_sphere_edits_preserve_old_marks_other_texture_set':True,'scope':'Modern documented functional outline; own controls and numeric curve sampling, no captured Painter9+ visual/default parity claim.'}
if a.surface_path:
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
if a.dynamic_stroke:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'assigned', 'expanded', 'cycle4', 'row0', 'random', 'row-random', 'undo', 'redo', 'parameters', 'world', 'saved', 'reopened', 'assigned-open', 'cycle-open', 'paint-open', 'undo-open', 'cleared-alpha']
    assert list(captures)==stages
    def data(stage):return captures[stage]['dynamic']
    def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
    def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
    d=data('assigned');assert d['resource']=='Square To Circle' and d['cycle']==32 and not d['random'] and abs(d['roundness']-.5)<1e-6 and abs(d['width']-.1)<1e-6 and not d['expanded']
    assert data('expanded')['expanded'] and data('cycle4')['cycle']==4
    phases=data('row0')['dabs'];assert len(phases)>=5 and phases[:5]==[0,.25,.5,.75,0],phases
    random=data('row-random')['dabs'];assert len(random)>=5 and random[0]!=phases[0] and len(set(random))==4 and random[0]==random[4],random
    assert data('parameters')['cycle']==4 and abs(data('parameters')['roundness']-.25)<1e-6 and abs(data('parameters')['width']-.2)<1e-6
    for before,after in [('setup','assigned'),('setup','expanded'),('setup','cycle4'),('row0','random'),('row0','undo'),('row-random','redo'),('row-random','parameters'),('world','saved'),('saved','reopened'),('reopened','assigned-open'),('reopened','cycle-open'),('reopened','undo-open'),('undo-open','cleared-alpha')]:
        for key in ['hashes','native_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
    for before,after in [('cycle4','row0'),('random','row-random'),('parameters','world'),('cycle-open','paint-open')]:
        assert data(before)['hashes'][0]!=data(after)['hashes'][0] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
        assert data(after)['undo_count']==data(before)['undo_count']+1,(before,after)
    assert data('assigned-open')['cycle']==32 and not data('assigned-open')['random'] and data('cleared-alpha')['resource']=='uniform color'
    for stage in stages[1:]:
        assert other(stage)==other('setup') and len(target(stage)['layers'])==len(target('setup')['layers']),stage
        assert data(stage)['old_mark']==data('setup')['old_mark'],stage
        if stages.index(stage)<13:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    result['dynamic_stroke_demo']={'pid':process.pid,'stages':stages,'shelf_dynamicstroke_search_and_original_outline_alpha':True,'stamp_index_cycle4_recurs_without_jitter':phases,'random_start_second_row_preserves_first':random,'controls_do_not_regenerate_existing_ink':True,'world_uv_atomic_undo_and_packed_open_further_paint':True,'source_mark_other_texture_set_layers_camera_preserved':True,'scope':'Own procedural Square To Circle outline; no Substance/SBSAR runtime or Painter exact sequence/seed parity claim.'}
if a.quick_mask:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'enabled', 'mask-painted', 'edit-off', 'restricted', 'cleared', 'undo', 'redo', 'enabled2', 'inverted', 'edit-off2', 'blocked', 'cleared2', 'filled-gap', 'undo-fill', 'saved-plain', 'enabled3', 'masked3', 'saved-masked', 'reopened', 'paint-open', 'undo-open']
    assert list(captures)==stages
    def data(stage):return captures[stage]['quick_mask']
    def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
    def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
    d=data('enabled');assert d['enabled'] and d['editing'] and d['size']==10 and d['flow']==100 and d['spacing']==20 and d['alpha']=='Shape' and d['gray']==0 and d['gray_visible'] and 'GRAYSCALE' in d['title']
    assert data('mask-painted')['center_allowed']<.01
    assert data('edit-off')['enabled'] and not data('edit-off')['editing'] and data('edit-off')['size']==15 and data('edit-off')['alpha']=='Sign Plus' and not data('edit-off')['gray_visible']
    assert data('restricted')['gap'][3]<.05*min(data('restricted')['left'][3],data('restricted')['right'][3]) and data('restricted')['left'][3]>.1 and data('restricted')['right'][3]>.1,data('restricted')
    assert data('restricted')['undo_count']==data('edit-off')['undo_count']+1
    assert data('inverted')['center_allowed']==0 and data('blocked')['center_allowed']==0
    assert data('filled-gap')['gap'][3]>.1,data('filled-gap')
    for before,after in [('setup','enabled'),('setup','mask-painted'),('setup','edit-off'),('setup','undo'),('restricted','cleared'),('restricted','redo'),('redo','enabled2'),('redo','inverted'),('redo','blocked'),('redo','cleared2'),('redo','undo-fill'),('saved-plain','enabled3'),('saved-plain','masked3'),('saved-plain','saved-masked'),('saved-masked','reopened'),('reopened','undo-open')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        assert data(before)['hashes']==data(after)['hashes'],(before,after)
    assert data('blocked')['undo_count']==data('edit-off2')['undo_count']
    assert data('reopened')['native_hash']==data('saved-plain')['native_hash'] and not data('reopened')['enabled'] and not data('reopened')['editing'] and data('reopened')['mask_count']==0
    assert data('paint-open')['hashes'][0]!=data('reopened')['hashes'][0]
    assert data('cleared')['native_hash']!=data('restricted')['native_hash'] and data('cleared')['gap']==data('restricted')['gap']
    for stage in stages[1:]:
        assert other(stage)==other('setup') and len(target(stage)['layers'])==len(target('setup')['layers']),stage
    result['quick_mask_demo']={'pid':process.pid,'stages':stages,'temporary_mask_edits_leave_document_and_layers_unchanged':True,'grayscale_brush_profile_and_ordinary_plus_restored':True,'world_mask_restricts_subsequent_world_stroke_with_near_empty_gap':True,'y_clears_preview_and_preserves_rejected_coverage':True,'i_inverts_selected_mask_and_blocks_entire_surface':True,'clear_allows_gap_fill_and_one_gesture_undo_restores_gap':True,'native_save_open_excludes_temporary_overlay_and_further_paint_undo_pass':True,'other_texture_set_data_unchanged':True}
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
if a.material_projection:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','entered','assigned','projected3d','paint-restored','projection-restored','transformed','projected-uv','undo','redo','tiling','standard','aces','uniform','weave-menu','paint-return','saved','reopened','entered-open','assigned-open','paint-open','undo-open','paint-final']
    assert list(captures)==stages
    def data(stage):return captures[stage]['projection']
    def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
    def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
    defaults=data('entered');assert defaults['active'] and defaults['source']=='uniform color'
    assert defaults['size']==10 and defaults['spacing']==20 and defaults['alpha']=='Shape' and defaults['pressure_size'] and defaults['minimum_size']==5
    assert defaults['flow']==defaults['opacity']==100 and defaults['angle']==0 and not defaults['follow'] and defaults['channels']==[True]*5 and defaults['tiling']==0
    for stage in ['assigned','projection-restored','weave-menu','assigned-open']:assert data(stage)['active'] and data(stage)['source']=='Blue Weave' and data(stage)['channels']==[True,True,True,False,True]
    assert abs(data('assigned')['roughness']-.8824)<1e-6
    for stage in ['paint-restored','paint-return']:assert not data(stage)['active'] and data(stage)['size']==15 and data(stage)['spacing']==100 and data(stage)['alpha']=='Sign Plus' and not data(stage)['pressure_size']
    assert data('projected3d')['source_error']<2e-4 and data('projected3d')['native_error']<2e-6 and data('projected3d')['samples']>=5,data('projected3d')
    assert data('transformed')['offset']!=[0,0] and data('transformed')['scale']>1 and abs(data('transformed')['rotation']-20)<1e-6
    assert data('tiling')['tiling']==3 and data('tiling')['active'] and data('tiling')['source']=='Blue Weave' and data('tiling')['hardness']==1 and data('uniform')['source']=='uniform color'
    control_pairs=[('setup','entered'),('entered','assigned'),('projected3d','paint-restored'),('paint-restored','projection-restored'),('projection-restored','transformed'),('redo','tiling'),('tiling','standard'),('standard','aces'),('aces','uniform'),('uniform','weave-menu'),('weave-menu','paint-return'),('reopened','entered-open'),('entered-open','assigned-open')]
    for before,after in control_pairs:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        for key in ['hash','native_hash','rough_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
    for before,after in [('assigned','projected3d'),('transformed','projected-uv'),('assigned-open','paint-open')]:assert data(before)['hash']!=data(after)['hash'] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
    for before,after in [('projected3d','undo'),('projected-uv','redo'),('redo','saved'),('saved','reopened'),('reopened','undo-open')]:
        for key in ['hash','native_hash','rough_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
    for stage in stages[1:]:
        assert other(stage)==other('setup'),stage
        assert len(target(stage)['layers'])==len(target('setup')['layers']),stage
        assert data(stage)['old_sample']==data('setup')['old_sample'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        if stages.index(stage)<18:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    result['material_projection_demo']={'pid':process.pid,'stages':stages,'independent_retained_paint_and_projection_profiles':True,'real_shelf_material_assignment_without_new_layers':True,'per_texel_source_color_and_roughness_max_error':data('projected3d')['source_error'],'native_composite_max_error':data('projected3d')['native_error'],'source_samples':data('projected3d')['samples'],'world_and_uv_screen_projection':True,'old_marks_and_other_texture_set_preserved':True,'s_transforms_tiling_and_brush_preset_keeps_projection_source':True,'ocio_overlay_without_transforming_authored_data':True,'undo_redo_packed_open_further_paint':True,'scope':'Original Blue Weave multichannel resource; no Fabric Denim/SBSAR runtime or physical particle-projection claim.'}
if a.follow_path:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','off3d','undo-off','follow-on','single-click','on3d','undo-on','redo-on','angle45','curved-uv','undo-curve','redo-curve','follow-off','click-off','undo-click','saved','reopened','follow-on-open','click-open','paint-open','undo-open']
    assert list(captures)==stages
    def data(stage):return captures[stage]['direction']
    assert not data('setup')['follow'] and data('setup')['angle']==0
    assert len(data('off3d')['dabs'])>=3 and all(d['orientation']==0 for d in data('off3d')['dabs'])
    assert data('follow-on')['follow'] and data('follow-on')['preview']!=data('setup')['preview']
    assert len(data('on3d')['dabs'])>=3 and sum(abs(d['orientation'])>.2 for d in data('on3d')['dabs'])>=3
    assert data('off3d')['hash']!=data('on3d')['hash'] and data('off3d')['native_hash']!=data('on3d')['native_hash']
    assert data('angle45')['angle']==45 and data('angle45')['follow']
    orientations=[d['orientation'] for d in data('curved-uv')['dabs']];assert len(orientations)>=5 and max(orientations)-min(orientations)>1
    assert all(d['view']=='2D' for d in data('curved-uv')['dabs'])
    for before,after in [('undo-off','follow-on'),('redo-on','angle45'),('redo-curve','follow-off'),('reopened','follow-on-open')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
    for before,after in [('follow-on','single-click'),('follow-on-open','click-open')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        for key in ['hash','native_hash','undo_count','redo_count','operations']:assert data(before)[key]==data(after)[key],(before,after,key)
        assert data(after)['dabs']==[]
    for before,after in [('setup','undo-off'),('setup','undo-on'),('on3d','redo-on'),('redo-on','undo-curve'),('curved-uv','redo-curve'),('redo-curve','undo-click'),('redo-curve','saved'),('saved','reopened'),('reopened','undo-open')]:
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
    for before,after in [('follow-off','click-off'),('click-open','paint-open')]:assert data(before)['hash']!=data(after)['hash'],(before,after)
    for stage in stages[1:]:
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        if stages.index(stage)<17:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    result['follow_path_demo']={'pid':process.pid,'stages':stages,'same_3d_route_off_on_changes_actual_alpha_orientation':True,'off_dabs':len(data('off3d')['dabs']),'on_dabs':len(data('on3d')['dabs']),'single_click_on_is_noop_including_history_and_redo':True,'base_angle_plus_multisegment_uv_directions':True,'preview_and_control_changes_preserve_existing_strokes':True,'undo_redo_packed_open_further_paint':True,'scope':'Own preceding-dab UV/local-world tangent formula; no claim of Painter smoothing or forward/backward orientation parity.'}
if a.stencil:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','empty-tiling','assigned','stencil3d','translated','rotated','scaled','cancelled-transform','reset','cleared','plain-stroke','undo-plain','shelf-drop','stencil2d','undo-uv','redo-uv','ignored-stroke','undo-ignored','bitmap-import','bitmap-stroke','undo-bitmap','saved','reopened','paint-open','undo-open']
    assert list(captures)==stages
    def data(stage):return captures[stage]['stencil']
    assert data('setup')['choices']==['No Tiling','Horizontal Tiling','Vertical Tiling','H and V Tiling']
    assert data('setup')['resource']=='' and data('setup')['tiling']==0
    assert data('empty-tiling')['tiling']==3 and data('empty-tiling')['resource']==''
    assert data('assigned')['resource']=='Sign Plus' and data('assigned')['scale']==.25
    assert data('stencil3d')['reference_error']<2e-6 and data('stencil3d')['white']>0 and data('stencil3d')['black']>0,data('stencil3d')
    assert data('translated')['offset']!=[0,0]
    assert abs(data('rotated')['angle']-35)<1e-6
    assert data('scaled')['scale']>data('rotated')['scale']
    for field in ['scale','angle','offset']:assert data('cancelled-transform')[field]==data('scaled')[field]
    assert data('reset')['scale']==1 and data('reset')['angle']==0 and data('reset')['offset']==[0,0]
    assert data('cleared')['resource']=='' and data('shelf-drop')['resource']=='Sign Plus'
    assert data('bitmap-import')['resource']=='half-stencil'
    for before,after in [('setup','empty-tiling'),('empty-tiling','assigned'),('stencil3d','translated'),('translated','rotated'),('rotated','scaled'),('scaled','cancelled-transform'),('cancelled-transform','reset'),('reset','cleared'),('undo-plain','shelf-drop'),('undo-ignored','bitmap-import')]:
        assert captures[before]['document']==captures[after]['document'],(before,after)
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
    for before,after in [('assigned','stencil3d'),('cleared','plain-stroke'),('shelf-drop','stencil2d'),('redo-uv','ignored-stroke'),('bitmap-import','bitmap-stroke'),('reopened','paint-open')]:
        assert data(before)['hash']!=data(after)['hash'] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
    for before,after in [('stencil3d','undo-plain'),('stencil3d','undo-uv'),('stencil2d','redo-uv'),('stencil2d','undo-ignored'),('stencil2d','undo-bitmap'),('stencil2d','saved'),('saved','reopened'),('reopened','undo-open')]:
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
    for stage in stages[1:]:
        assert not data(stage)['ignore']
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        if stages.index(stage)<23:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    result['stencil_demo']={'pid':process.pid,'stages':stages,'per_texel_3d_camera_mask_max_error':data('stencil3d')['reference_error'],'real_resource_and_shelf_drop':True,'image_import_and_non_square_resource':True,'four_tiling_choices_and_empty_noop':True,'s_translate_rotate_scale_escape_and_reset_preserve_paint_and_camera':True,'uv_masking_and_temporary_n_bypass':True,'undo_redo_and_packed_open':True,'scope':'Own viewport-space scalar stencil; legacy empty state plus current Adobe gesture contract. Transform defaults and overlay appearance are not claimed as measured Painter parity.'}
if a.jitter:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','size-control','size3d','flow-control','flow3d','position-control','position3d','angle-control','angle3d','all-controls','all2d','undo','redo','saved','reopened','open-controls','paint-open','undo-open','zero-control','zero2d','undo-zero']
    assert list(captures)==stages
    def data(stage):return captures[stage]['jitter']
    expected={'setup':[0,0,0,0],'size-control':[80,0,0,0],'flow-control':[0,80,0,0],'position-control':[0,0,0,100],'angle-control':[0,0,90,0],'all-controls':[80,80,90,100],'zero-control':[0,0,0,0]}
    for stage,settings in expected.items():assert data(stage)['settings']==settings,(stage,data(stage))
    for before,after in [('setup','size-control'),('size3d','flow-control'),('flow3d','position-control'),('position3d','angle-control'),('angle3d','all-controls'),('reopened','open-controls'),('undo-open','zero-control')]:
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
        assert captures[before]['document']==captures[after]['document'],(before,after)
    def spread(values):return max(values)-min(values)
    dab_counts={}
    for stage,field in [('size3d','size'),('flow3d','flow'),('position3d','position'),('angle3d','angle')]:
        dabs=data(stage)['dabs'];dab_counts[stage]=len(dabs);assert len(dabs)>=4,(stage,dabs)
        assert all(d['view']=='3D' for d in dabs)
        for dab in dabs:
            assert .2-1e-6<=dab['size']<=1 and .2-1e-6<=dab['flow']<=1 and abs(dab['angle'])<=math.pi/2 and math.hypot(*dab['position'])<=1+1e-6
            if field!='size':assert dab['size']==1
            if field!='flow':assert dab['flow']==1
            if field!='angle':assert dab['angle']==0
            if field!='position':assert dab['position']==[0,0]
        if field=='position':assert spread([d['position'][1] for d in dabs])>.2
        else:assert spread([d[field] for d in dabs])>.2,(stage,dabs)
    for before,after in [('size-control','size3d'),('flow-control','flow3d'),('position-control','position3d'),('angle-control','angle3d'),('all-controls','all2d'),('open-controls','paint-open'),('zero-control','zero2d')]:
        assert data(before)['hash']!=data(after)['hash'] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
    for before,after in [('angle3d','undo'),('all2d','redo'),('all2d','saved'),('saved','reopened'),('reopened','undo-open'),('reopened','undo-zero')]:
        assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
    for stage in ['all2d','paint-open','zero2d']:assert data(stage)['dabs'] and all(d['view']=='2D' for d in data(stage)['dabs'])
    assert all(d['size']==d['flow']==1 and d['angle']==0 and d['position']==[0,0] for d in data('zero2d')['dabs'])
    assert data('size-control')['preview']!=data('setup')['preview']
    for stage in stages[1:]:
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage not in ['reopened','open-controls','paint-open','undo-open','zero-control','zero2d','undo-zero']:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    result['jitter_demo']={'pid':process.pid,'stages':stages,'independent_size_flow_angle_position_dabs_in_3d':dab_counts,'uv_jitter_and_zero_identity':True,'preview_updates_without_rewriting_existing_strokes':True,'native_texture_changes_for_actual_strokes':True,'undo_redo_and_packed_open_preserve_results':True,'scope':'Original seeded per-dab size/flow attenuation, angle rotation and disk scatter; no claim of Painter distribution/range/seed parity.'}
if a.instance_cycles:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'empty-folder', 'copy-empty', 'root-instance', 'root-hierarchy', 'empty-source-nav', 'paint-selected', 'grouped', 'folder-content', 'copy-folder', 'child-selected', 'nested-instance', 'cycle-hierarchy', 'undo-cycle', 'saved-valid', 'redo-cycle', 'hierarchy-redo', 'saved-cycle', 'opened-cycle', 'cycle-content', 'cycle-hierarchy-open', 'opened-valid', 'root-content', 'root-hierarchy-again', 'source-again'];assert list(captures)==stages
    def data(stage):return captures[stage]['instance']
    def cycle(stage):return data(stage)['cycle']
    active=['nested-instance','cycle-hierarchy','redo-cycle','hierarchy-redo','saved-cycle','opened-cycle','cycle-content','cycle-hierarchy-open'];warning=['cycle-hierarchy','hierarchy-redo','saved-cycle','opened-cycle','cycle-hierarchy-open']
    for stage in stages[1:]:
        d=data(stage);c=cycle(stage);assert d['native_error']<2e-6 and d['horizontal_scroll']==0 and not c['modal'],stage
        assert d['hashes'][1]==data('setup')['hashes'][1],stage
        if stages.index(stage)>=9:assert d['hashes'][0]==data('folder-content')['hashes'][0],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        assert c['folder_problem']==c['nested_problem']==(stage in active),stage
        assert c['warning_visible']==(stage in active and d['hierarchy']),stage
        if stage in active:
            assert c['nested_parent'] and c['nested_depth']==1 and c['problem_count']>=2,stage
            if stage in warning:assert 'Cyclic' in c['warning'] and d['hierarchy'] and not d['paint_enabled'],stage
        else:assert not d['errors'],stage
    assert data('undo-cycle')['selected']==cycle('undo-cycle')['child'] and data('undo-cycle')['paint_enabled']
    assert data('opened-cycle')['undo_count']==data('opened-valid')['undo_count']==0
    assert data('root-content')['empty'] and data('root-hierarchy-again')['hierarchy'] and data('source-again')['selected']==cycle('source-again')['empty']
    for file in ['valid-folders.blend','cyclic-folders.blend']:assert (a.output/file).is_file()
    result['instance_cycle_demo']={'pid':process.pid,'stages':stages,'empty_folder_instance_and_source_hierarchy_work':True,'self_dependent_folder_instance_retained_selectable_but_not_rendered':True,'source_and_instance_warnings_visible_without_modal':True,'undo_creation_restores_paint_child_and_removes_warning':True,'valid_and_cyclic_packed_blend_roundtrips':True,'material_and_target_paint_unchanged_during_cycle':True,'scope':'Direct self-dependent folder cycle and Undo recovery, with cycle/valid Save/Open. Indirect cycles and repair by moving/deleting have no separate native walkthrough here.'}
if a.paint_instance:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'paint-layer', 'stroke-before', 'copied', 'target-selected', 'instanced', 'content-target', 'hierarchy', 'source-nav', 'source-content', 'stroke-after', 'undo-after', 'redo-after', 'target-return', 'saved', 'reopened', 'hierarchy-open', 'source-open', 'content-open', 'stroke-open', 'undo-open', 'redo-open', 'target-open'];assert list(captures)==stages
    def data(stage):return captures[stage]['instance']
    def paint(stage):return data(stage)['paint_boundary']
    target_hash=data('paint-layer')['hashes'][1]
    for stage in stages[1:]:
        d=data(stage);assert d['native_error']<2e-6 and not d['errors'] and d['horizontal_scroll']==0,stage
        if stage!='setup':assert d['hashes'][1]==target_hash,stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        if stage not in ['setup','paint-layer']:
            expected=1 if stages.index(stage)<=10 or stage=='undo-after' else 3 if stage in ['stroke-open','redo-open','target-open'] else 2
            assert paint(stage)['operations']==expected and paint(stage)['all_spatial'] and paint(stage)['target_evaluated_empty'],stage
        if stage in ['instanced','content-target','hierarchy','target-return','saved','reopened','hierarchy-open','target-open']:
            assert d['set']==1 and not d['paint_enabled'] and not d['effects_enabled'] and not d['fill_properties'],stage
    assert data('setup')['hashes'][0]!=data('stroke-before')['hashes'][0]!=data('stroke-after')['hashes'][0]
    assert paint('stroke-before')['source_hash']==paint('undo-after')['source_hash']!=paint('stroke-after')['source_hash']
    for stage in ['redo-after','target-return','saved','reopened','source-open','content-open','undo-open']:assert paint(stage)['source_hash']==paint('stroke-after')['source_hash'],stage
    assert data('reopened')['undo_count']==0 and (a.output/'paint-instances.blend').is_file()
    result['paint_instance_demo']={'pid':process.pid,'stages':stages,'spatial_strokes_before_and_after_instancing_remain_on_source_set_only':True,'independent_target_plus_and_procedural_instance_pixel_hash_unchanged':True,'target_paint_instance_enabled_but_read_only_and_raster_empty':True,'source_hierarchy_navigation_and_packed_save_open':True,'undo_redo_and_further_spatial_paint_after_open':True,'scope':'Cross-set brush painting boundary; same-set paint and cross-set Paint effects/masks have CPU evaluation support, not separate native walkthroughs here.'}
if a.instances:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup-source', 'target-undercoat', 'target-painted', 'source-selected', 'copied', 'target-selected', 'paste-unfocused', 'instanced', 'hierarchy', 'source-navigated', 'content-source', 'border20', 'undo', 'redo', 'restored', 'target-return', 'content-target', 'saved', 'reopened', 'hierarchy-open', 'source-open', 'content-open', 'edited-open', 'undo-open', 'redo-open', 'target-open'];assert list(captures)==stages
    def data(stage):return captures[stage]['instance']
    def doc(stage,index):return captures[stage]['document']['texture_sets'][index]
    target_stages=['instanced','hierarchy','target-return','content-target','saved','reopened','hierarchy-open','target-open'];hierarchy=['hierarchy','source-navigated','hierarchy-open','source-open']
    for stage in stages[1:]:
        d=data(stage);assert d['native_error']<2e-6 and not d['errors'] and d['horizontal_scroll']==0,stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup-source']['native'][key],(stage,key)
        assert captures[stage]['view']['scene_view_matrix']==captures['setup-source']['view']['scene_view_matrix'],stage
        assert d['hierarchy']==(stage in hierarchy),stage
        if stage in hierarchy:assert d['hierarchy_roots']==1 and d['hierarchy_children']==1,stage
        if stage in target_stages:
            assert d['set']==1 and d['selected']==d['created'] and not d['paint_enabled'] and not d['effects_enabled'] and not d['fill_properties'],stage
            assert d['empty']==(stage not in hierarchy),stage
        if stages.index(stage)>=8:
            source=next(l for l in doc(stage,0)['layers'] if '{'+l['id']+'}'==d['source']);target=next(l for l in doc(stage,1)['layers'] if '{'+l['id']+'}'==d['created'])
            assert source['blending'][0]=='Screen' and target['blending'][0]=='Passthrough',stage
            assert target['instance']=={'set':doc(stage,0)['id'],'layer':source['id']},stage
            assert d['paint_hashes'][1][:-1]==data('target-painted')['paint_hashes'][1],stage
    for before,after in [('source-selected','copied'),('target-selected','paste-unfocused')]:assert captures[before]['document']==captures[after]['document'] and data(before)['undo_count']==data(after)['undo_count'],(before,after)
    assert data('border20')['hashes'][0]!=data('restored')['hashes'][0] and data('border20')['hashes'][1]!=data('restored')['hashes'][1]
    for stage in ['instanced','undo','restored','target-return','content-target','saved','reopened','undo-open']:assert data(stage)['hashes']==data('instanced')['hashes'],stage
    for stage in ['border20','redo','edited-open','redo-open','target-open']:assert data(stage)['hashes']==data('border20')['hashes'],stage
    assert data('reopened')['undo_count']==0 and (a.output/'instances.blend').is_file()
    result['instance_demo']={'pid':process.pid,'stages':stages,'linked_triplanar_atom_source_updates_both_native_textures':True,'source_screen_and_target_passthrough_are_independent':True,'focused_shortcuts_and_unfocused_paste_boundary':True,'target_content_read_only_and_hierarchy_mouse_navigation':True,'independent_target_paint_preserved':True,'undo_redo_and_packed_save_open':True,'scope':'Procedural Fill instancing and hierarchy navigation; folder and cycle CPU coverage, spatial paint boundary and cycle UI continued in PD-068/069.'}
if a.hide_excluded:
    from PIL import Image
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'hidden', 'stroke', 'off', 'geometry', 'included', 'content', 'undo', 'redo', 'excluded-again', 'hidden-again', 'saved', 'reopened', 'off-open', 'geometry-open', 'included-open', 'content-open', 'undo-open', 'redo-open'];extra=["mesh-type","mesh-excluded","mesh-content","mesh-hidden","tile-type-again","scene-only-retire","split-after-retire","shown-after-retire"] if a.visibility_shape else [];assert list(captures)==stages+extra
    def data(stage):return captures[stage]['geometry_mask']
    red=[.8,.1,.05,1];included=['included','content','redo','included-open','content-open','redo-open'];hidden=['hidden','stroke','hidden-again','saved','reopened']
    for stage in stages[1:]:
        d=data(stage);assert d['hide_toggle']==d['native_hidden']==(stage in hidden),stage
        if stage not in included:assert max(abs(a-b) for a,b in zip(d['left'],red))<3e-7,(stage,d['left'])
        if stage not in ['setup','hidden']:
            assert min(d['right'][:3])>.8 and d['paint_hash']==data('stroke')['paint_hash'],stage
            if stage in included:assert min(d['left'][:3])>.8,stage
        assert d['horizontal_scroll']==0,stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        for key in ['scene_view_matrix','uv_center','uv_vertical_extent']:assert captures[stage]['view'][key]==captures['setup']['view'][key],(stage,key)
    for key in ['left_paint_hash','right_paint_hash']:assert data('setup')[key]!=data('stroke')[key],key
    for before,after in [('setup','hidden'),('stroke','off'),('excluded-again','hidden-again')]:assert captures[before]['document']==captures[after]['document'] and data(before)['undo_count']==data(after)['undo_count'],(before,after)
    # Real framebuffer probes: hidden native surfaces expose the background in both panes.
    def pixel(stage,key):
        point=data(stage)[key];im=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');x,y=map(round,point);return im.getpixel((x,y))
    for key in ['scene_probe','uv_probe']:
        assert max(abs(a-b) for a,b in zip(pixel('setup',key),pixel('hidden',key)))>40,(key,pixel('setup',key),pixel('hidden',key))
        assert max(abs(a-b) for a,b in zip(pixel('hidden',key),pixel('stroke',key)))<8,(key,pixel('hidden',key),pixel('stroke',key))
        assert max(abs(a-b) for a,b in zip(pixel('hidden',key),pixel('off',key)))>40,key
    assert data('reopened')['undo_count']==0 and (a.output/'hidden-tiles.blend').is_file()
    result['hide_excluded_demo']={'pid':process.pid,'stages':stages,'native_transparency_hides_excluded_surface_in_3d_and_uv':True,'visibility_toggles_preserve_document_and_history':True,'spatial_stroke_retained_on_both_tiles_even_while_hidden':True,'reinclude_and_undo_restore_stroke_contribution':True,'packed_save_open_retains_paint':True,'scope':'Legacy two-tile hide/ignore workflow with retained spatial strokes; no overlapping front/back mesh or camera-change projection claim.'}
    if a.visibility_shape:
        for stage in stages[1:]+extra:
            assert data(stage)['visibility_images']==(0 if stage=='setup' else 1),(stage,data(stage)['visibility_images'])
        for stage in extra:
            d=data(stage);left_white=stage in ['mesh-type','mesh-excluded','mesh-content','mesh-hidden'];right_white=stage not in ['mesh-excluded','mesh-content','mesh-hidden']
            for key,white in [('left',left_white),('right',right_white)]:
                if white:assert min(d[key][:3])>.8,(stage,key,d[key])
                else:assert max(abs(a-b) for a,b in zip(d[key],red))<3e-7,(stage,key,d[key])
            assert d['paint_hash']==data('stroke')['paint_hash'] and d['horizontal_scroll']==0,stage
            assert d['hide_toggle']==(stage in ['mesh-hidden','tile-type-again','scene-only-retire','split-after-retire']),stage
            assert d['native_hidden']==(stage in ['mesh-hidden','scene-only-retire','split-after-retire']),stage
            assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['scene_probe_right','uv_probe_right']:assert max(abs(a-b) for a,b in zip(pixel('mesh-content',key),pixel('mesh-hidden',key)))>40,key
        for key in ['scene_probe','uv_probe']:assert max(abs(a-b) for a,b in zip(pixel('mesh-content',key),pixel('mesh-hidden',key)))<8,key
        assert len(captures['scene-only-retire']['view']['panes'])==1 and len(captures['split-after-retire']['view']['panes'])==2
        result['visibility_shape_demo']={'pid':process.pid,'stages':extra,'tile_to_mesh_to_tile_images_retire_without_extra_users':True,'exactly_one_tracked_visibility_image_after_every_render':True,'borrowed_uv_ids_released_even_in_3d_only_layout':True,'native_mesh_exclusion_and_split_recovery':True}
if a.tile_mask:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'painted', 'geometry', 'tile-type', 'exclude-1001', 'exclude-1002', 'undo-1002', 'content', 'undo-1001', 'geometry-return', 'exclude-again', 'saved', 'reopened', 'content-open', 'included-open', 'undo-open', 'redo-open', 'restore-all'];assert list(captures)==stages
    def data(stage):return captures[stage]['geometry_mask']
    red=[.8,.1,.05,1];painted_left=data("painted")["left"];painted_right=data("painted")["right"]
    assert min(painted_left[:3])>.99 and min(painted_right[:3])>.99
    all_red=['setup','exclude-1002'];all_white=['painted','geometry','tile-type','undo-1001','geometry-return','included-open','redo-open','restore-all']
    editor_off=['setup','painted','content','undo-1001','content-open']
    for stage in stages[1:]:
        expected=[red,red] if stage in all_red else [painted_left,painted_right] if stage in all_white else [red,painted_right]
        for key,value in zip(['left','right'],expected):assert max(abs(a-b) for a,b in zip(data(stage)[key],value))<3e-7,(stage,key,data(stage)[key])
        editing=stage not in editor_off;tile_editing=editing and stage!='geometry'
        assert data(stage)['selected']==data(stage)['properties']==editing and data(stage)['hatching']==tile_editing and data(stage)['paint_enabled']==(not editing),stage
        excluded=2 if stage=='exclude-1002' else 1 if tile_editing and stage not in all_white else 0
        assert data(stage)['excluded']==excluded and data(stage)['horizontal_scroll']==0,stage
        if excluded:assert data(stage)['hatch_shapes']>=2*excluded,stage
        if stage!='setup':assert data(stage)['paint_hash']==data('painted')['paint_hash'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
        assert captures[stage]['document']['texture_sets'][0]['tiles']==[1001,1002],stage
        for key in ['scene_view_matrix','uv_center','uv_vertical_extent']:assert captures[stage]['view'][key]==captures['setup']['view'][key],(stage,key)
        if tile_editing:assert data(stage)['count']==f'{2-excluded} / 2 included',stage
    assert data('reopened')['undo_count']==0 and (a.output/'tile-mask.blend').is_file()
    result['tile_mask_demo']={'pid':process.pid,'stages':stages,'per_tile_contribution_and_separate_checkbox_undo':True,'original_cross_tile_paint_hash_retained':True,'hatching_only_in_edit_mode':True,'packed_blend_and_further_undo':True,'scope':'UV-tile contribution gating with 2D tile hatching and projected mesh feedback. No claim of skipped GPU allocation or export omission.'}
if a.geometry_mask:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'geometry', 'right-excluded', 'undo', 'redo', 'excluded-all', 'left-only', 'copied', 'included-all', 'pasted', 'undo-paste', 'redo-paste', 'saved', 'reopened', 'included-open', 'undo-open', 'content-target', 'geometry-return', 'paint-mask', 'geometry-with-mask', 'mask-target', 'geometry-again', 'exclude-menu', 'undo-menu'];assert list(captures)==stages
    def data(stage):return captures[stage]['geometry_mask']
    blue=[.05,.2,.8,1];red=[.8,.1,.05,1]
    all_blue=['setup','geometry','undo','included-all','undo-paste','included-open']
    all_red=['excluded-all','exclude-menu']
    editor_off=['setup','content-target','paint-mask','mask-target']
    for stage in stages[1:]:
        expected=[blue,blue] if stage in all_blue else [red,red] if stage in all_red else [blue,red]
        for key,value in zip(['left','right'],expected):assert max(abs(a-b) for a,b in zip(data(stage)[key],value))<3e-7,(stage,key,data(stage)[key],value)
        assert data(stage)['selected']==data(stage)['properties']==(stage not in editor_off),stage
        assert data(stage)['horizontal_scroll']==0 and sorted(data(stage)['names'])==['LeftPart','RightPart'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
    assert captures['setup']['native']['mesh_objects']==2 and captures['setup']['native']['uv_triangles']==4
    for before,after in [('left-only','copied'),('pasted','redo-paste'),('saved','reopened'),('reopened','undo-open'),('geometry-again','undo-menu')]:assert data(before)['hash']==data(after)['hash'],(before,after)
    assert data('copied')['undo_count']==data('left-only')['undo_count']
    assert data('reopened')['undo_count']==0 and data('mask-target')['mask_selected']
    assert (a.output/'geometry-mask.blend').is_file()
    result['geometry_mask_demo']={'pid':process.pid,'stages':stages,'named_uv_ownership_and_flat_texture_values':True,'include_exclude_copy_paste_and_undo':True,'packed_blend_roundtrip':True,'content_and_paint_mask_exit_editor':True,'scope':'Named mesh geometry contribution on non-overlapping UV islands; shared UV texels remain shared. Modern documentation-backed feature, not a measured Painter 6.2 layout.'}
if a.anchor_navigation:
    import copy
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'go-source', 'collapsed', 'expanded', 'consumer-list', 'go-source-again', 'consumer-key', 'second-layer', 'second-black', 'second-fill', 'second-linked', 'second-go-source', 'second-activated', 'first-go-source', 'first-activated', 'clear-first', 'remaining-source', 'remaining-activated', 'clear-second', 'empty-source', 'empty-collapsed', 'empty-expanded', 'undo-second', 'undo-first', 'saved', 'reopened', 'activated-open', 'go-open'];assert list(captures)==stages
    def data(stage):return captures[stage]['anchor_navigation']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def authoring(stage):
        value=copy.deepcopy(doc(stage))
        for key in ['selected_layer','selected_mask','selected_effect']:value.pop(key)
        return value
    source_stages=['go-source','collapsed','expanded','go-source-again','second-go-source','first-go-source','remaining-source','empty-source','empty-collapsed','empty-expanded','undo-second','undo-first','saved','reopened','go-open']
    first_stages=['setup','consumer-list','consumer-key','first-activated','clear-first']
    second_stages=['second-layer','second-black','second-fill','second-linked','second-activated','remaining-activated','clear-second','activated-open']
    sphere=captures['setup']['document']['texture_sets'][1]
    for stage,c in list(captures.items())[1:]:
        assert data(stage)['horizontal_scroll']==0 and c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        if stage not in ['second-layer','second-black']:
            uniform=stage in ['second-fill','clear-first','clear-second'];assert data(stage)['native_valid'],stage
            assert abs(data(stage)['value']-(.5 if uniform else 0))<1e-6 and abs(data(stage)['other']-(.5 if uniform else 1))<1e-6,stage
            if not uniform:assert data(stage)['hash']==data('setup')['hash'],stage
        if stage in source_stages:
            assert data(stage)['selected_parent']==data(stage)['source'] and data(stage)['selected_effect']==data(stage)['anchor'] and data(stage)['anchor_properties'] and not data(stage)['jump_visible'],stage
            count=0 if stage.startswith('empty-') else 1 if stage in ['go-source','collapsed','expanded','go-source-again','remaining-source','undo-second'] else 2
            assert data(stage)['references']==count,stage
            expanded=stage not in ['collapsed','empty-collapsed'];assert data(stage)['expanded']==expanded and data(stage)['references_visible']==(expanded and count>0) and data(stage)['empty_visible']==(expanded and count==0),stage
        if stage in first_stages:assert data(stage)['selected_parent']==data(stage)['first'] and data(stage)['selected_effect']==data(stage)['first_fill'],stage
        if stage in second_stages:
            assert data(stage)['selected_parent']==data(stage)['second'],stage
            if stage not in ['second-layer','second-black']:assert data(stage)['selected_effect']==data(stage)['second_fill'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    navigation_pairs=[('setup','go-source'),('go-source','collapsed'),('collapsed','expanded'),('expanded','consumer-list'),('consumer-list','go-source-again'),('go-source-again','consumer-key'),('second-linked','second-go-source'),('second-go-source','second-activated'),('second-activated','first-go-source'),('first-go-source','first-activated'),('clear-first','remaining-source'),('remaining-source','remaining-activated'),('clear-second','empty-source'),('empty-source','empty-collapsed'),('empty-collapsed','empty-expanded'),('reopened','activated-open'),('activated-open','go-open')]
    for before,after in navigation_pairs:
        assert authoring(before)==authoring(after) and data(before)['undo_count']==data(after)['undo_count'] and data(before)['redo_count']==data(after)['redo_count'],(before,after)
        assert captures[before]['view']['scene_view_matrix']==captures[after]['view']['scene_view_matrix'],(before,after)
    assert authoring('second-linked')==authoring('undo-first')==authoring('saved')==authoring('reopened')==authoring('go-open')
    assert data('reopened')['undo_count']==data('activated-open')['undo_count']==data('go-open')['undo_count']==0
    assert (a.output/'navigation.blend').is_file()
    result['anchor_navigation_demo']={'pid':process.pid,'stages':stages,'fill_button_navigates_to_exact_source_uuid':True,'consumer_list_mouse_and_keyboard_activation_select_exact_fill':True,'two_real_consumers_navigate_independently':True,'empty_and_populated_disclosures_preserve_selection_and_pixels':True,'navigation_adds_no_undo_redo_or_authoring_changes':True,'camera_unchanged_during_navigation':True,'clear_inputs_updates_reference_list_and_undo_restores_it':True,'packed_open_navigation_without_history_edits':True,'sphere_and_geometry_unchanged':True,'scope':'Working mask-anchor navigation. Source/consumer control placement is our interface; the installed Painter consumer navigation was not successfully observed.'}
if a.anchor_validity:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'saved-valid', 'higher', 'undo-higher', 'disabled', 'undo-disabled', 'deleted', 'undo-delete', 'deleted-again', 'same-name', 'undo-name', 'undo-new', 'undo-original', 'cross-set', 'deleted-save', 'saved-broken', 'opened-broken', 'cleared', 'undo-clear', 'restored-valid', 'long-name', 'saved-long', 'reopened-long', 'restore-name', 'undo-rename'];assert list(captures)==stages
    def data(stage):return captures[stage]['anchor_validity']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def source(stage):return next(l for l in doc(stage)['layers'] if l['id']==data(stage)['source'].strip('{}'))
    broken=['higher','disabled','deleted','deleted-again','same-name','undo-name','undo-new','deleted-save','saved-broken','opened-broken','undo-clear']
    sphere=captures['setup']['document']['texture_sets'][1];source_content=source('setup')['effects']
    for stage,c in list(captures.items())[1:]:
        other=0 if stage in broken else .5 if stage=='cleared' else 1;value=.5 if stage=='cleared' else 0
        assert abs(data(stage)['value']-value)<1e-6 and abs(data(stage)['other']-other)<1e-6,(stage,data(stage))
        assert data(stage)['warning']==(stage in broken) and data(stage)['uniform_visible']==(stage=='cleared'),stage
        assert data(stage)['horizontal_scroll']==0 and c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert source(stage)['effects']==source_content and source(stage)['paint_hashes']==source('setup')['paint_hashes'],stage
        assert data(stage)['reference']==('' if stage=='cleared' else data(stage)['anchor']),stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage in broken:assert 'input is treated as black' in data(stage)['warning_text'],stage
    assert data('higher')['source_exists'] and data('higher')['source_enabled'] and 'lower layer' in data('higher')['warning_text']
    assert data('disabled')['source_exists'] and not data('disabled')['source_enabled'] and 'disabled' in data('disabled')['warning_text']
    for stage in ['deleted','same-name','opened-broken']:assert not data(stage)['source_exists'] and 'missing' in data(stage)['warning_text'],stage
    assert data('same-name')['local_anchors']==1 and not data('same-name')['source_exists'] and data('undo-new')['local_anchors']==0
    assert data('cross-set')['cross_set_rejected'] and data('cross-set')['cross_set_preserved']
    assert doc('setup')==doc('saved-valid')==doc('undo-higher')==doc('undo-disabled')==doc('undo-delete')==doc('undo-original')==doc('cross-set')==doc('restored-valid')==doc('restore-name')
    assert doc('deleted-save')==doc('saved-broken')==doc('opened-broken')==doc('undo-clear')
    assert doc('long-name')==doc('saved-long')==doc('reopened-long')==doc('undo-rename') and len(data('long-name')['source_name'])==120
    for name in ['valid.blend','broken.blend','long-name.blend']:assert (a.output/name).is_file()
    result['anchor_validity_demo']={'pid':process.pid,'stages':stages,'source_above_consumer_breaks_input_and_undo_repairs_uuid':True,'disabled_or_removed_source_warns_and_supplies_black_input':True,'same_name_new_anchor_never_rebinds_old_reference':True,'cross_texture_set_assignment_rejected_atomically_by_controller':True,'broken_reference_persists_across_save_open':True,'clear_and_undo_restore_missing_reference':True,'explicit_valid_project_restore_repairs_input':True,'long_names_persist_without_horizontal_scroll':True,'sphere_source_content_and_geometry_unchanged':True,'scope':'Working mask-anchor rules with an original warning UI and black missing-input fallback. Captured Painter runtime error UI was not established.'}
if a.anchor_reference:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'created', 'renamed', 'consumer', 'black-mask', 'fill', 'linked', 'source-painted', 'undo-source', 'redo-source', 'source-restored', 'source-prefix', 'undo-prefix', 'references', 'opacity50', 'full', 'saved', 'reopened', 'edited-open', 'undo-open', 'redo-open', 'restored-open', 'anchor-selected'];assert list(captures)==stages
    def data(stage):return captures[stage]['anchor_reference']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def layer(stage,id):return next(l for l in doc(stage)['layers'] if l['id']==id.strip('{}'))
    def source(stage):return layer(stage,data(stage)['source'])
    def consumer(stage):return layer(stage,data(stage)['consumer'])
    sphere=captures['setup']['document']['texture_sets'][1];source_content=source('setup')['effects']
    for stage,c in list(captures.items())[1:]:
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert source(stage)['effects']==source_content and source(stage)['paint_hashes']==source('setup')['paint_hashes'],stage
        assert data(stage)['horizontal_scroll']==0 and data(stage)['source_own_value']==0,stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage not in ['consumer','black-mask']:
            assert data(stage)['native_valid'],stage
            value=.5 if stage=='fill' else 1 if stage=='source-prefix' else 0
            other=.5 if stage in ['fill','opacity50'] else 1
            probe=0 if stage in ['source-painted','redo-source','edited-open','redo-open'] else other
            assert max(abs(a0-b0) for a0,b0 in zip([data(stage)['value'],data(stage)['other'],data(stage)['probe']],[value,other,probe]))<1e-6,(stage,data(stage))
        if stage in ['created','renamed','references','anchor-selected']:
            assert data(stage)['anchor_properties'] and not data(stage)['paint_enabled'],stage
            assert data(stage)['references']==(0 if stage in ['created','renamed'] else 1),stage
            assert data(stage)['no_references']==(stage in ['created','renamed']),stage
        if stage in ['linked','source-painted','undo-source','redo-source','source-restored','source-prefix','undo-prefix','opacity50','full','saved','reopened','edited-open','undo-open','redo-open','restored-open']:
            assert not data(stage)['uniform_visible'] and not data(stage)['mapping_enabled'] and not data(stage)['warning'] and not data(stage)['paint_enabled'],stage
            assert 'Cube mask reference' in data(stage)['resource'],stage
    assert data('created')['hash']==data('setup')['hash']==data('renamed')['hash']
    assert data('renamed')['anchor_name']=='Cube mask reference' and len(doc('created')['layers'])==len(doc('setup')['layers'])
    marker=next(e for e in source('renamed')['mask']['effects'] if e['type']=='anchor');assert marker['name']=='Cube mask reference' and marker['id']==data('renamed')['anchor'].strip('{}')
    assert data('linked')['hash']==data('setup')['hash'] and data('source-painted')['hash']!=data('linked')['hash']
    assert doc('linked')==doc('undo-source')==doc('source-restored')==doc('undo-prefix')==doc('full')==doc('saved')==doc('reopened')==doc('undo-open')==doc('restored-open')
    assert doc('source-painted')==doc('redo-source')==doc('edited-open')==doc('redo-open')
    assert (a.output/'anchors.blend').is_file()
    result['anchor_reference_demo']={'pid':process.pid,'stages':stages,'named_mask_anchor_is_noop_at_its_stack_position':True,'real_fill_resource_links_by_uuid_to_lower_mask_anchor':True,'source_strokes_refresh_dependent_native_texture':True,'anchor_exports_prefix_not_parent_final_mask':True,'source_reference_list_counts_real_consumer':True,'effect_opacity_preserves_reference':True,'packed_open_further_source_paint_undo_redo':True,'sphere_source_content_and_geometry_unchanged':True,'scope':'Mask Fill consumes anchors on strictly lower layers in the same Texture Set, using matching UVs. Content/PBR anchor sources and transformed sampling are not claimed; installed Painter consumer interactions remain unverified.'}
if a.fill_effect:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'default', 'gray25', 'under-paint', 'undo-order', 'redo-order', 'restored', 'mapping', 'unlocked', 'scaleU4', 'scaleV2', 'rotated', 'offset', 'frame-off', 'frame-on', 'atom', 'scale-atom', 'nearest', 'reset', 'repeat-shift', 'clamp', 'border', 'uniform', 'saved', 'reopened', 'edited-open', 'undo-open', 'redo-open', 'restored-open', 'under-open', 'undo-under'];assert list(captures)==stages
    def data(stage):return captures[stage]['fill_effect']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def effect(stage):return next(e for e in doc(stage)['layers'][-1]['mask']['effects'] if e['id']==doc(stage)['selected_effect'])
    sphere=captures['setup']['document']['texture_sets'][1];content=doc('setup')['layers'][-1]['effects'];uniform_stages=stages[2:16]+stages[23:]
    for stage,c in list(captures.items())[1:]:
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert len(doc(stage)['layers'])==len(doc('setup')['layers']) and doc(stage)['layers'][-1]['effects']==content,stage
        assert c['inspection']==5 and data(stage)['horizontal_scroll']==0,stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage!='setup':
            e=effect(stage);assert e['type']=='fill' and not data(stage)['paint_enabled'] and data(stage)['frame_button'],stage
            assert data(stage)['projection']=='UV projection' and data(stage)['v_enabled']==(not e['mapping']['linked']),stage
            assert data(stage)['frame']==e['mapping']['manipulator'],stage
        if stage in uniform_stages:
            value=.5 if stage=='default' else .4 if stage in ['edited-open','redo-open'] else .25
            ink=0 if stage in ['under-paint','redo-order','under-open'] else value
            assert abs(data(stage)['value']-ink)<1e-6 and abs(data(stage)['other']-value)<1e-6,(stage,data(stage))
    defaults=effect('default');assert defaults['value']==.5 and defaults['opacity']==1 and defaults['blending']=='Normal'
    mapping=defaults['mapping'];assert mapping['scale']==[1,1] and mapping['offset']==[0,0] and mapping['rotation']==0 and mapping['linked'] and mapping['filtering']==0 and mapping['wrap']==0 and mapping['manipulator']
    assert effect('offset')['mapping']['scale']==[4,2] and effect('offset')['mapping']['rotation']==30 and effect('offset')['mapping']['offset']==[.125,.25]
    assert not data('frame-off')['frame'] and data('frame-on')['frame']
    for stage in stages[7:16]:assert data(stage)['hash']==data('gray25')['hash'],stage
    assert data('atom')['hash']!=data('scale-atom')['hash']!=data('nearest')['hash']
    assert any(0<v<.25 for v in data('atom')['samples'])
    for stage in ['nearest','reset','repeat-shift']:assert set(data(stage)['samples'])<={0,.25},stage
    assert data('reset')['hash']==data('repeat-shift')['hash'] and data('repeat-shift')['middle']==.25
    assert set(data('clamp')['samples'])==set(data('border')['samples'])=={0} and data('clamp')['hash']==data('border')['hash']
    assert doc('gray25')==doc('undo-order')==doc('restored')==doc('mapping') and doc('under-paint')==doc('redo-order')
    assert doc('saved')==doc('reopened')==doc('undo-open')==doc('restored-open')==doc('undo-under') and doc('edited-open')==doc('redo-open')
    assert (a.output/'fill-effect.blend').is_file()
    result['fill_effect_demo']={'pid':process.pid,'stages':stages,'opaque_fill_05_then_025_hides_paint_without_deleting_it':True,'alt_arrow_below_paint_reveals_black_stroke_on_gray':True,'uv_projection_defaults_mapping_controls_and_frame_toggle':True,'original_atom_resource_responds_to_uv_transforms':True,'hq_samples_and_nearest_repeat_clamp_black_border':True,'packed_open_further_value_edit_undo_and_order_undo':True,'content_effects_sphere_geometry_unchanged':True,'scope':'Grayscale mask Fill. Original analytic Atom with four-tap HQ sampling; proprietary resource/filtering pixel parity and content-side multichannel Fill effects are not claimed.'}
if a.filter_effect:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'empty', 'inverted', 'below-paint', 'undo-move', 'redo-move', 'restored', 'opacity50', 'full', 'posterize', 'steps2', 'empty-again', 'content-empty', 'content-invert', 'content-below-paint', 'undo-content', 'saved', 'reopened', 'edited-open', 'undo-open', 'redo-open', 'restored-open', 'filter-off', 'filter-on'];assert list(captures)==stages
    def data(stage):return captures[stage]['filter_effect']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def top(stage):return doc(stage)['layers'][-1]
    def selected(stage):return next(e for e in (top(stage)['mask']['effects'] if doc(stage)['selected_mask'] else top(stage)['effects']) if e['id']==doc(stage)['selected_effect'])
    ink=[1,.1,.05];inverted=[0,.9,.95];errors={};sphere=captures['setup']['document']['texture_sets'][1]
    mask_values={'inverted':(1,0),'below-paint':(0,0),'undo-move':(1,0),'redo-move':(0,0),'restored':(1,0),'opacity50':(.5,.5),'full':(1,0)}
    for stage,c in list(captures.items())[1:]:
        values=mask_values.get(stage,(0,1));assert max(abs(a0-b0) for a0,b0 in zip([data(stage)['mask_value'],data(stage)['mask_other']],values))<1e-6,(stage,data(stage),values)
        expected=inverted if stage in ['content-invert','undo-content','saved','reopened','undo-open','restored-open','filter-on'] else [.5,.5,.5] if stage in ['edited-open','redo-open'] else ink
        if not doc(stage)['selected_mask']:
            errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['color'][:3],expected));assert errors[stage]<3e-7,(stage,data(stage)['color'],expected)
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert len(doc(stage)['layers'])==len(doc('setup')['layers']) and top(stage)['paint_hashes']==top('setup')['paint_hashes'],stage
        assert data(stage)['horizontal_scroll']==0,stage
        if stage!='setup':
            effect=selected(stage);assert effect['type']=='filter' and not data(stage)['paint_enabled'] and data(stage)['properties'],stage
            assert data(stage)['steps_visible']==(effect['filter']['resource']==3),stage
            assert data(stage)['channel_visible']==(not doc(stage)['selected_mask'] and effect['filter']['resource']>0),stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    assert selected('empty')['filter']=={'resource':0,'steps':4} and selected('empty')['opacity']==1 and selected('empty')['blending']=='Normal'
    assert data('setup')['mask_hash']==data('empty')['mask_hash']==data('empty-again')['mask_hash']
    assert doc('inverted')==doc('undo-move')==doc('restored') and doc('below-paint')==doc('redo-move')
    assert doc('content-invert')==doc('undo-content')==doc('saved')==doc('reopened')==doc('undo-open')==doc('restored-open')==doc('filter-on')
    assert doc('edited-open')==doc('redo-open') and selected('steps2')['filter']['steps']==2
    assert (a.output/'filters.blend').is_file()
    result['filter_effect_demo']={'pid':process.pid,'stages':stages,'native_color_errors':errors,'empty_resource_preserves_input_disables_paint_tools':True,'original_invert_changes_real_mask_and_material_pixels':True,'alt_arrow_order_above_below_paint_changes_output':True,'opacity_posterize_parameters_and_clear_resource':True,'effect_eye_and_undo':True,'packed_open_further_edit_undo_redo':True,'sphere_and_geometry_unchanged':True,'scope':'Original working-value Invert, Clamp and Posterize point filters. No proprietary SBSAR runtime or Blur pixel-parity claim; the captured Painter reference only proves empty insertion and reorder.'}
if a.paint_effect:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'mask-empty', 'mask-stroke', 'undo-mask', 'redo-mask', 'mask-off', 'mask-on', 'below-compare', 'undo-order', 'mask50', 'mask100', 'content-empty', 'content-stroke', 'undo-content', 'redo-content', 'content50', 'content100', 'multiply', 'normal', 'saved', 'reopened', 'paint-open', 'undo-open', 'redo-open', 'erase-open', 'undo-erase', 'cancelled', 'mask-inspected', 'paint3d', 'undo3d'];assert list(captures)==stages
    def data(stage):return captures[stage]['paint_effect']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def top(stage):return doc(stage)['layers'][-1]
    def maskfx(stage):return top(stage)['mask']['effects']
    def contentfx(stage):return top(stage)['effects']
    base=[.8,.6,.2];ink=[1,.1,.05];errors={};sphere=captures['setup']['document']['texture_sets'][1]
    white=['setup','mask-empty','undo-mask','mask-off','below-compare'];color_stages=['content-stroke','redo-content','content100','normal','saved','reopened','paint-open','undo-open','redo-open','undo-erase','cancelled','mask-inspected','paint3d','undo3d']
    for stage,c in list(captures.items())[1:]:
        value=1 if stage in white else .5 if stage=='mask50' else 0;assert abs(data(stage)['mask_value']-value)<1e-6,(stage,data(stage)['mask_value'],value)
        assert (stage=='paint3d' or data(stage)['mask_other']==1) and data(stage)['parent_ink']==data('setup')['parent_ink'] and data(stage)['parent_mask_ink']==data('setup')['parent_mask_ink'],stage
        expected=ink if stage in color_stages else [(a0+b0)/2 for a0,b0 in zip(base,ink)] if stage=='content50' else [a0*b0 for a0,b0 in zip(base,ink)] if stage=='multiply' else base
        errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['color'][:3],expected));assert errors[stage]<3e-7,(stage,data(stage)['color'],expected)
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert len(doc(stage)['layers'])==len(doc('setup')['layers']) and contentfx(stage)[0]==contentfx('setup')[0],stage
        assert data(stage)['horizontal_scroll']==0,stage
        if stage!='setup':assert data(stage)['paint_enabled'] and data(stage)['mix_visible'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    assert len(maskfx('mask-empty'))==2 and maskfx('mask-empty')[-1]['type']=='paint' and maskfx('mask-empty')[0]['type']=='compare'
    assert len(contentfx('content-empty'))==2 and contentfx('content-empty')[-1]['type']=='paint' and len(maskfx('content-empty'))==2
    for stage in stages[2:12]:assert data(stage)['gray_visible'],stage
    for stage in stages[12:28]:assert not data(stage)['gray_visible'],stage
    assert doc('mask-stroke')==doc('redo-mask')==doc('mask-on')==doc('undo-order')
    assert doc('content-stroke')==doc('redo-content')==doc('content100')==doc('normal')==doc('saved')==doc('reopened')==doc('undo-open')
    assert doc('paint-open')==doc('redo-open')==doc('undo-erase')==doc('cancelled')
    assert data('mask-stroke')['mask_hash']!=data('undo-mask')['mask_hash'] and data('content-stroke')['color_hash']!=data('undo-content')['color_hash']
    assert data('paint3d')['mask_ink']!=data('mask-inspected')['mask_ink'] and data('undo3d')['mask_ink']==data('mask-inspected')['mask_ink']
    assert data('paint-open')['low'][0]>.99 and data('undo-open')['low'][0]<.9
    assert data('paint-open')['mask_ink']==data('reopened')['mask_ink']
    assert (a.output/'paint-effects.blend').is_file()
    result['paint_effect_demo']={'pid':process.pid,'stages':stages,'native_color_errors':errors,'independent_sparse_mask_and_content_contributions':True,'actual_uv_and_3d_strokes':True,'stroke_undo_redo_and_cancel':True,'effect_eye_opacity_and_blending':True,'alt_arrow_order_over_compare_mask':True,'parent_base_rasters_unchanged':True,'mask_and_content_effect_chains_preserved_separately':True,'packed_open_further_edit_erase_undo':True,'sphere_and_geometry_unchanged':True,'scope':'Effect paint remains UV-bound during UV reprojection. Session Undo retains entire strokes; gesture replay for effect rasters is not implemented.'}
if a.compare_mask:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial', 'setup', 'fresh', 'constant-greater', 'less', 'tolerance', 'wide-tolerance', 'identical', 'layer-less', 'below-greater', 'lower-edited', 'undo-lower', 'this-layer', 'current-edited', 'undo-current', 'soft', 'hard', 'mask-prefix', 'prefix-edited', 'undo-prefix', 'saved', 'reopened', 'edited-open', 'undo-open', 'redo-open', 'restored', 'effect-off', 'effect-on'];assert list(captures)==stages
    def data(stage):return captures[stage]['compare_mask']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def effect(stage):return next(e for e in doc(stage)['layers'][-1]['mask']['effects'] if e['type']=='compare')
    expected=dict(zip(stages[1:],[.25,0,0,1,0,1,1,1,1,0,1,0,1,0,.5,0,1,0,1,1,1,0,1,0,1,.25,1]))
    errors={};sphere=captures['setup']['document']['texture_sets'][1]
    for stage,c in list(captures.items())[1:]:
        errors[stage]=abs(data(stage)['value']-expected[stage]);assert errors[stage]<2e-6,(stage,data(stage),expected[stage])
        frame=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');pixel=frame.getpixel((frame.width*3//4,frame.height//2));assert max(abs(component-255*expected[stage]) for component in pixel)<=1,(stage,pixel,expected[stage])
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert not data(stage)['paint_enabled'],stage
        assert doc(stage)['layers'][-1]['effects']==doc('setup')['layers'][-1]['effects'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage!='setup':
            assert data(stage)['properties'] and data(stage)['channels']==[0,1,2,3,4] and data(stage)['horizontal_scroll']==0,stage
            assert data(stage)['constant_visible']==(effect(stage)['left']==3 or effect(stage)['right']==3),stage
            assert data(stage)['tolerance_visible']==(effect(stage)['operation']==1),stage
            assert data(stage)['identical_warning']==(effect(stage)['left']==effect(stage)['right']),stage
    assert effect('fresh')['channel']==4 and effect('fresh')['left']==1 and effect('fresh')['right']==0 and effect('fresh')['operation']==2
    assert abs(effect('fresh')['hardness']-.9)<1e-6 and abs(effect('fresh')['tolerance']-.1)<1e-6
    assert len(doc('fresh')['layers'][-1]['mask']['effects'])==1 and doc('fresh')['layers'][-1]['blending'][4]=='Normal'
    assert not data('identical')['channel_enabled'] and data('wide-tolerance')['channel_enabled']
    assert doc('saved')==doc('reopened')==doc('undo-open')==doc('restored') and doc('edited-open')==doc('redo-open')
    for left,right in [('lower-edited','undo-lower'),('this-layer','current-edited'),('mask-prefix','prefix-edited')]:assert data(left)['hash']!=data(right)['hash']
    assert (a.output/'compare-mask.blend').is_file()
    result['compare_mask_demo']={'pid':process.pid,'stages':stages,'native_scalar_errors':errors,'three_operators_and_identical_operand_warning':True,'live_cross_channel_lower_and_current_content_inputs':True,'preceding_mask_input':True,'atomic_replacement_preserves_content_effects_resets_height_blend':True,'hardness_tolerance_and_dynamic_controls':True,'dependency_cache_reacts_to_both_content_sources':True,'packed_open_further_edit_undo_redo':True,'sphere_and_geometry_unchanged':True,'scope':'PBR/AO operands; own linear luminance and smooth comparison boundaries. Unmeasured legacy boundary/HDR pixel parity is not claimed.'}
if a.levels:
    import math
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','fresh','red','green','blue','global','undo-global','redo-global','reset-all','auto','hdr','clamped','reset-base','roughness','rough-invert','rough-clamp','rough-reset','color','rgb-restored','saved','reopened','component-red','component-blue','edited','undo','redo','restored','midtone-handle','undo-handle','channels-removed','undo-channels'];assert list(captures)==stages
    base=[.2,.4,.8];complex_color=[.8,.6,math.sqrt(.8)];global_color=[.2,.4,math.sqrt(.2)]
    expected={stage:base[:] for stage in stages[1:]};expected.update({'red':[.8,.4,.8],'green':[.8,.6,.8],'blue':complex_color,'global':global_color,'undo-global':complex_color,'redo-global':global_color,'auto':[0,1/3,1],'hdr':[-1,0,2],'clamped':[0,0,1]})
    for stage in ['rgb-restored','saved','reopened','component-red','component-blue','undo','restored','undo-handle','channels-removed','undo-channels']:expected[stage]=complex_color
    for stage in ['edited','redo','midtone-handle']:expected[stage]=[.8,.6,.8]
    def data(stage):return captures[stage]['levels']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def effect(stage):return doc(stage)['layers'][-1]['effects'][0]
    errors={};sphere=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1]
    for stage,c in list(captures.items())[1:]:
        errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['samples'][:3],expected[stage]));assert errors[stage]<3e-7,(stage,data(stage)['samples'],expected[stage])
        rough=.75 if stage in ['rough-invert','rough-clamp'] else .25;assert abs(data(stage)['roughness']-rough)<1e-7,stage
        assert data(stage)['samples'][3]==1 and not data(stage)['paint_enabled'],stage
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'] and doc(stage)['layers'][:-1]==lower,stage
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        if stage!='setup':
            assert len(doc(stage)['layers'][-1]['effects'])==1 and effect(stage)['id']==effect('fresh')['id'],stage
            assert data(stage)['horizontal_scroll']==0 and data(stage)['clamp_visible'],stage
            if effect(stage)['channel']==2:assert data(stage)['components']==['Lum'],stage
            else:assert data(stage)['components']==['Color','Red','Green','Blue'],stage
            assert data(stage)['channels']==([0,2,3] if stage=='channels-removed' else [0,1,2,3,4]),stage
    assert [effect('fresh')[k] for k in ['input_low','gamma','input_high','output_low','output_high','clamp']]==[0,1,1,0,1,False]
    assert effect('rough-reset')['channel']==2 and not effect('rough-reset')['clamp']
    assert abs(effect('auto')['input_low']-.2)<1e-7 and abs(effect('auto')['input_high']-.8)<1e-7
    assert effect('green')['rgb_curves'][0]['output_low']==1 and effect('green')['rgb_curves'][1]['output_low']==1
    assert effect('blue')['rgb_curves'][2]['gamma']==2
    assert doc('global')==doc('redo-global') and data('blue')['hash']==data('undo-global')['hash']
    assert doc('saved')==doc('reopened')==doc('component-blue')==doc('undo')==doc('restored')==doc('undo-handle')==doc('undo-channels')
    assert doc('edited')==doc('redo')==doc('midtone-handle')
    assert data('reopened')['undo_count']==data('component-red')['undo_count']==data('component-blue')['undo_count']==0
    assert data('component-red')['hash']==data('component-blue')['hash']==data('reopened')['hash']
    result['levels_demo']={'pid':process.pid,'stages':stages,'native_rgb_errors':errors,'independent_rgb_curves_survive_editor_component_changes_and_open':True,'global_curve_runs_before_component_curves':True,'component_selection_does_not_create_undo_entries':True,'auto_input_range_and_signed_hdr_clamp':True,'same_effect_retargets_roughness_lum_and_reset_keeps_channel':True,'actual_midtone_handle_and_undo':True,'channel_choices_follow_active_set_channels':True,'compact_controls_clamp_visible_no_horizontal_scroll':True,'packed_open_further_edit_undo_redo':True,'sphere_lower_layers_geometry_camera_unchanged':True,'scope':'Observed defaults/channel/invert/clamp/reset follow the reference. RGB curves compose in linear working values; Auto uses the sampled input range. Unmeasured legacy RGB/gamma/Auto pixel parity is not claimed.'}
if a.mesh_generator:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','empty','height-missing','position','position-flipped','undo-input','redo-input','restored-input','axis-x','undo-axis','contrast','undo-contrast','curvature-missing','curvature','curvature-flipped','undo-curvature','redo-curvature','restored-curvature','invalid','cancelled','saved','reopened','height-open','undo-open','redo-open','restored','generator-off','generator-on','opacity50','undo-opacity','inverted','undo-invert'];assert list(captures)==stages
    def data(stage):return captures[stage]['mesh_generator']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def generator(stage):return next(e for e in doc(stage)['layers'][-1]['mask']['effects'] if e['type']=='generator')
    errors={};sphere=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1]
    for stage,c in list(captures.items())[1:]:
        expected=[.75]*16
        if stage!='setup':
            e=generator(stage);assert e['blending']=='Normal'
            role=5 if e['generator']==1 else 4;source=doc(stage)['mesh_maps'][role]
            if e['enabled'] and e['generator'] and source:
                flipped='flipped' in source['name'];values=[]
                for x in range(4,128,8):
                    if e['generator']==1:
                        value=round(255*x/127)/255
                        rgb=[51/255,204/255,1-value if flipped else value];raw=rgb[e['axis']]
                    else:
                        raw=(32 if x<64 else 224)/255
                        if flipped:raw=(224 if x<64 else 32)/255
                    value=max(0,min(1,(raw-.5)*e['contrast']+.5+e['bias']));value=1-value if e['invert'] else value;values.append(.75*(1-e['opacity'])+value*e['opacity'])
                expected=values
            assert data(stage)['properties'] and not data(stage)['paint_enabled'],stage
            assert data(stage)['controls']==bool(e['generator']),stage
            if e['generator']:assert data(stage)['axis_visible']==(e['generator']==1),stage
        errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['samples'],expected));assert errors[stage]<2e-7,(stage,data(stage)['samples'],expected)
        assert c['inspection']==5 and c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert doc(stage)['layers'][:-1]==lower and c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    assert data('setup')['hash']==data('empty')['hash']==data('height-missing')['hash']==data('curvature-missing')['hash']==data('generator-off')['hash']
    assert doc('position')==doc('undo-input')==doc('restored-input')==doc('undo-axis')==doc('undo-contrast') and doc('position-flipped')==doc('redo-input')
    assert data('position')['hash']!=data('position-flipped')['hash']
    assert doc('curvature')==doc('undo-curvature')==doc('restored-curvature')==doc('invalid')==doc('cancelled')==doc('saved')==doc('reopened')==doc('undo-open')==doc('restored')==doc('generator-on')==doc('undo-opacity')==doc('undo-invert')
    assert doc('curvature-flipped')==doc('redo-curvature') and data('curvature')['hash']!=data('curvature-flipped')['hash']
    assert data('position')['hash']==data('height-open')['hash']==data('redo-open')['hash']
    e=generator('empty');assert [e[k] for k in ['generator','axis','contrast','bias','invert','opacity','blending']]==[0,2,1,0,False,1,'Normal']
    result['mesh_generator_demo']={'pid':process.pid,'stages':stages,'native_scalar_errors':errors,'empty_generator_preserves_existing_mask_and_hides_parameters':True,'original_world_height_and_curvature_generators_use_real_inputs':True,'actual_file_dialog_substitution_changes_mask_and_undo_restores_exact_document':True,'axis_contrast_invert_opacity_and_eye':True,'invalid_input_and_cancel_preserve_document':True,'packed_mesh_inputs_open_and_generator_switch_undo_redo':True,'sphere_lower_layers_geometry_camera_unchanged':True,'scope':'Original mask-only generators: World Height reads a normalized Position axis; Curvature reads the baked red channel. Common contrast/bias/invert and blending are defined by this app. Missing inputs are indicated and pass through. No legacy Light/SBSAR or content-side generator parity is claimed.'}
if a.mask_effects:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','fill-default','fill25','levels-default','inverted','below-fill','undo-move','redo-move','above-fill','levels-off','levels-on','fill50','undo-opacity','clamped','reset','inverted-again','saved','reopened','moved-open','undo-open','redo-open','restored'];assert list(captures)==stages
    def data(stage):return captures[stage]['mask_effects']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def effects(stage):return doc(stage)['layers'][-1]['mask']['effects']
    expected={'setup':0,'fill-default':.5,'fill25':.25,'levels-default':.25,'inverted':.75,'below-fill':.25,'undo-move':.75,'redo-move':.25,'above-fill':.75,'levels-off':.25,'levels-on':.75,'fill50':.875,'undo-opacity':.75,'clamped':.75,'reset':.25,'inverted-again':.75,'saved':.75,'reopened':.75,'moved-open':.25,'undo-open':.75,'redo-open':.25,'restored':.75};errors={}
    sphere=captures['setup']['document']['texture_sets'][1];hashes={}
    for stage,c in list(captures.items())[1:]:
        value=expected[stage];errors[stage]=abs(data(stage)['value']-value);assert errors[stage]<1e-7,(stage,data(stage)['value'],value)
        frame=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');pixel=frame.getpixel((frame.width*3//4,frame.height//2));assert max(abs(component-255*value) for component in pixel)<=1,(stage,pixel,value)
        if value in hashes:assert data(stage)['hash']==hashes[value],stage
        else:hashes[value]=data(stage)['hash']
        assert c['inspection']==5 and not data(stage)['paint_enabled'],stage
        selected=next(e for e in effects(stage) if e['id']==doc(stage)['selected_effect'])
        if selected['type']=='levels':assert data(stage)['levels_properties'] and not data(stage)['levels_channel_visible'],stage
        if selected['type']=='fill':assert data(stage)['fill_properties'],stage
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    assert data('inverted')['order']==data('undo-move')['order']==['levels','fill','selection']
    assert data('below-fill')['order']==data('redo-move')['order']==['fill','levels','selection']
    assert doc('inverted')==doc('undo-move') and doc('below-fill')==doc('redo-move')
    assert doc('saved')==doc('reopened')==doc('undo-open')==doc('restored') and doc('moved-open')==doc('redo-open')
    reset=next(e for e in effects('reset') if e['type']=='levels')['levels'];assert not reset['clamp'] and [reset[k] for k in ['input_low','gamma','input_high','output_low','output_high']]==[0,1,1,0,1] and reset['channel']==2
    result['mask_effects_demo']={'pid':process.pid,'stages':stages,'native_scalar_errors':errors,'same_value_native_mask_hashes_identical':True,'rendered_uv_grayscale_matches_within_one_byte':True,'levels_above_fill_075_below_fill_025':True,'actual_alt_arrow_moves_selection_and_undo_redo_restore_order':True,'fill_opacity_and_effect_eye':True,'mask_levels_has_no_affected_channel_row':True,'invert_clamp_reset_and_mask_histogram':True,'packed_open_reorder_undo_redo':True,'sphere_geometry_camera_unchanged':True}
if a.mask_replace:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','white','undo','redo','saved','reopened','black','undo-black','removed','undo-remove'];assert list(captures)==stages
    def data(stage):return captures[stage]['mask_replace']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def mask(stage):return doc(stage)['layers'][-1]['mask']
    assert len(mask('setup')['effects'])==1 and mask('setup')['background']==0 and min(data('setup')['samples'])<.01 and max(data('setup')['samples'])>.99
    assert doc('setup')==doc('undo') and data('setup')['hash']==data('undo')['hash']==data('black')['hash']
    for stage in ['white','redo','saved','reopened','undo-black','removed','undo-remove']:
        assert all(abs(v-.5)<1e-6 for v in data(stage)['samples']),stage
        if stage!='removed':assert mask(stage)['background']==1 and mask(stage)['effects']==[] and mask(stage)['tiles']==[] and mask(stage)['enabled'] and doc(stage)['selected_effect']=='',stage
    assert mask('black')['background']==0 and mask('black')['effects']==[] and mask('removed') is None
    assert doc('white')==doc('redo')==doc('saved')==doc('reopened')==doc('undo-black')==doc('undo-remove')
    sphere=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1]
    for stage,c in list(captures.items())[1:]:
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'] and doc(stage)['layers'][:-1]==lower,stage
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    result['mask_replace_demo']={'pid':process.pid,'stages':stages,'white_and_black_replace_old_mask_and_clear_effect_chain_atomically':True,'single_undo_restores_exact_old_document_and_native_pattern':True,'removal_and_undo':True,'packed_save_open':True,'lower_layers_sphere_geometry_camera_unchanged':True}
if a.smart_mask:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','painted','preset-saved','shelf','destination','merged','undo-merge','redo-merge','ctrl-replaced','undo-replace','redo-replace','without-mask','created','undo-create','redo-create','saved','reopened','paint-open','undo-paint','redo-paint','restored']
    assert list(captures)==stages
    def data(stage):return captures[stage]['smart_mask']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def layer(stage,key):return next(v for v in doc(stage)['layers'] if v['id']==data(stage)[key].strip('{}'))
    h0=data('painted')['native_hash'];paint_hash=data('painted')['mask_hash'];asset=data('preset-saved')['asset']
    assert asset['paint_hash']==paint_hash and asset['scope']==3 and asset['name']=='Vertical mask fixture'
    assert doc('painted')==doc('preset-saved')==doc('shelf')
    assert data('destination')['native_hash']!=h0
    merged=data('merged')['native_hash'];assert merged not in [h0,data('destination')['native_hash']]
    assert data('redo-merge')['native_hash']==data('undo-replace')['native_hash']==merged
    assert doc('destination')==doc('undo-merge') and doc('merged')==doc('redo-merge')==doc('undo-replace')
    effects=layer('merged','target')['mask']['effects'];assert len(effects)==3 and effects[0]['colors']==[[0,1,0]] and effects[1]['type']=='paint' and effects[2]['colors']==[[1,0,0]]
    assert len({e['id'] for e in effects})==3 and effects[2]['id']!=layer('merged','source')['mask']['effects'][0]['id']
    for stage in ['ctrl-replaced','redo-replace','created','redo-create','saved','reopened','undo-paint','restored']:
        assert data(stage)['native_hash']==h0 and data(stage)['mask_hash']==paint_hash,stage
    assert len(layer('ctrl-replaced','target')['mask']['effects'])==1
    assert not data('without-mask')['has_mask'] and not data('undo-create')['has_mask'] and layer('created','empty')['mask']['background']==0
    assert doc('without-mask')==doc('undo-create') and doc('created')==doc('redo-create')
    assert doc('saved')==doc('reopened')==doc('undo-paint')==doc('restored') and doc('paint-open')==doc('redo-paint')
    assert data('paint-open')['native_hash']==data('redo-paint')['native_hash']!=h0
    sphere=captures['setup']['document']['texture_sets'][1]
    for stage,c in list(captures.items())[1:]:
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    library=a.smart_mask_library or a.output/'mask-library';metadata=json.loads((library/'library.json').read_text());entries=[r for r in metadata['resources'] if r['name']=='Vertical mask fixture'];assert len(entries)==1 and entries[0]['sha256']==asset['hash']
    if data('preset-saved')['loaded']:assert data('preset-saved')['loaded_id']==asset['id']
    result['smart_mask_demo']={'pid':process.pid,'stages':stages,'asset':asset,'loaded_from_user_library_in_this_process':data('preset-saved')['loaded'],'real_context_save_and_smart_masks_shelf':True,'actual_drop_merges_paint_and_ordered_effects':True,'ctrl_drop_replaces_with_original_native_mask_exactly':True,'drop_without_mask_creates_black_mask':True,'single_step_undo_redo_for_each_application':True,'packed_open_further_paint_and_undo_redo':True,'sphere_geometry_camera_unchanged':True,'scope':'Black-background presets retain authored paint and color-selection effects. Merge preserves destination background/toggle, overlays authored ink and appends effects; Ctrl replaces. Version3 preserves uniform backgrounds; authored merge ink is a GPU Paint stage. Legacy Painter smart-mask reapplication was not observed; documented workflow is tested with our original preset.'}
if a.mask_copy:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','painted','copied','destination','pasted','undo','redo','source-edited','snapshot-pasted','fresh-copied','fresh-pasted','undo-fresh','redo-fresh','saved','reopened','paint-open','undo-paint','redo-paint','restored']
    assert list(captures)==stages
    def data(stage):return captures[stage]['mask_copy']
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def layer(stage,key):return next(v for v in doc(stage)['layers'] if v['id']==data(stage)[key].strip('{}'))
    original=data('painted')['source_hash'];modified=data('source-edited')['source_hash'];assert original!=modified
    h0=data('painted')['native_mask_hash'];h1=data('source-edited')['native_mask_hash'];assert h0!=h1
    assert min(data('painted')['samples'])==0 and max(data('painted')['samples'])>.9
    assert doc('painted')==doc('copied') and not data('painted')['clipboard'] and data('copied')['clipboard']
    assert layer('painted','source')['mask']['effects'][0]['enabled'] is False
    for stage in ['pasted','redo','snapshot-pasted','undo-fresh']:
        assert data(stage)['destination_hash']==original and data(stage)['native_mask_hash']==h0,stage
    assert data('source-edited')['source_hash']==data('snapshot-pasted')['source_hash']==modified
    for stage in ['fresh-pasted','redo-fresh','saved','reopened','undo-paint','restored']:
        assert data(stage)['source_hash']==data(stage)['destination_hash']==modified and data(stage)['native_mask_hash']==h1,stage
    assert data('paint-open')['native_mask_hash']==data('redo-paint')['native_mask_hash']!=h1
    assert doc('destination')==doc('undo') and doc('pasted')==doc('redo')
    assert doc('snapshot-pasted')==doc('undo-fresh') and doc('fresh-pasted')==doc('redo-fresh')
    assert doc('saved')==doc('reopened')==doc('undo-paint')==doc('restored') and doc('paint-open')==doc('redo-paint')
    source_effect=layer('pasted','source')['mask']['effects'][0];destination_effect=layer('pasted','destination')['mask']['effects'][0]
    assert source_effect['id']!=destination_effect['id'] and {k:v for k,v in source_effect.items() if k!='id'}=={k:v for k,v in destination_effect.items() if k!='id'}
    sphere=captures['setup']['document']['texture_sets'][1]
    for stage,c in list(captures.items())[1:]:
        assert c['inspection']==5,stage
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    menus=data('restored')['menus'];assert menus[0]=={'stage':'copied','copy':True,'paste':False} and all(m['copy'] and m['paste'] for m in menus[1:])
    assert [m['stage'] for m in menus]==['copied','pasted','snapshot-pasted','fresh-copied','fresh-pasted']
    result['mask_copy_demo']={'pid':process.pid,'stages':stages,'real_context_menu_enabled_state':menus,'native_mask_image_matches_source_exactly':True,'clipboard_is_independent_snapshot_after_source_edit':True,'effects_replaced_with_fresh_ids_without_rasterizing':True,'packed_open_and_actual_further_mask_stroke_undo_redo':True,'sphere_geometry_camera_unchanged':True,'scope':'Defined replacement semantics preserve the destination bypass flag and retained authoring resolution; incompatible UV tile sets reject atomically. The reference establishes named menu routes, not exact unmeasured legacy clipboard internals.'}
if a.color_selection:
    import math
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','created','id-uv','red-picked','id-3d','both-picked','green-removed','output25','tolerance1','soft','undo-soft','redo-soft','bypassed','enabled','saved','reopened','edited','undo-open','redo-open','restored','resource-cleared','resource-restored','effect-removed','undo-remove','cancelled']
    assert list(captures)==stages
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def mask(stage):return doc(stage)['layers'][-1]['mask']
    def effect(stage):return mask(stage)['effects'][0]
    def data(stage):return captures[stage]['color_selection']
    sphere=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1];errors={}
    for stage,c in list(captures.items())[1:]:
        m=mask(stage);e=m['effects'][0] if m and m['effects'] else None;expected=[]
        for color in [[1,0,0],[0,1,0]]:
            value=1 if not m or not m['enabled'] else m['background']
            if m and m['enabled'] and e and e['enabled']:
                value=0
                if e['use_id_map'] and e['colors']:
                    distance=min(math.sqrt(sum((a0-b0)**2 for a0,b0 in zip(color,selected))/3) for selected in e['colors'])
                    if distance<=e['tolerance']*e['hardness']+1e-8:value=e['output']
                    elif distance<e['tolerance']:
                        t=(e['tolerance']-distance)/(e['tolerance']*(1-e['hardness']));value=e['output']*t*t*(3-2*t)
            expected.append(value)
        actual=data(stage)['samples'];errors[stage]=max(abs(actual[i][c0]-expected[i]) for i in range(2) for c0 in range(3));assert errors[stage]<2e-6,(stage,actual,expected)
        assert all(abs(v[3]-1)<1e-6 for v in actual),stage
        assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
        assert doc(stage)['layers'][:-1]==lower and doc(stage)['mesh_maps']==doc('setup')['mesh_maps'],stage
        assert doc(stage)['layers'][-1]['paint_pages']==[0]*5,stage
        if m:assert m['tiles']==[],stage
        if e:assert not data(stage)['paint_enabled'] and data(stage)['properties_visible'],stage
        assert data(stage)['picker']==(stage in ['id-uv','id-3d']),stage
        assert data(stage)['map']==(2 if stage in ['id-uv','id-3d'] else -1),stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    assert effect('created')['colors']==[] and [effect('created')[k] for k in ['output','hardness','tolerance']]==[1,1,.10000000149011612]
    assert effect('red-picked')['colors']==[[1,0,0]] and effect('both-picked')['colors']==[[1,0,0],[0,1,0]]
    assert effect('green-removed')['colors']==[[1,0,0]]
    assert doc('soft')==doc('redo-soft') and doc('tolerance1')==doc('undo-soft')
    assert doc('saved')==doc('reopened')==doc('undo-open')==doc('restored')==doc('resource-restored')==doc('undo-remove')==doc('cancelled')
    assert doc('edited')==doc('redo-open')
    result['color_selection_demo']={'pid':process.pid,'stages':stages,'native_masked_rgb_max_errors':errors,'empty_black_mask_and_separate_effect_created_from_mask_menu':True,'uv_red_and_native_3d_green_picking_restore_previous_inspection':True,'multi_color_remove_output_tolerance_hardness_bypass':True,'packed_id_map_and_parameters_open_edit_undo_redo':True,'resource_binding_remove_rebind_and_effect_remove_undo':True,'escape_restores_inspection_without_document_edit':True,'sphere_lower_layers_geometry_camera_and_paint_pages_unchanged':True,'scope':'Reference verifies creation and empty defaults only. Real selection extends that evidence with native tests; normalized RGB distance and smooth falloff are our defined behavior, not claimed as exact Painter tolerance equivalence.'}
if a.passthrough:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','empty-normal','passthrough','fresh-levels','inverted','normal-control','passthrough-restored','effect-off','effect-on','roughness-target','roughness-reset','base-inverted','saved','reopened','edited','undo','redo','undo-again','removed','undo-remove','clamped','reset-base']
    assert list(captures)==stages
    def doc(stage):return captures[stage]['document']['texture_sets'][0]
    def witness(stage):return captures[stage]['passthrough']
    baseline=witness('setup')['samples'];lower=doc('setup')['layers'];sphere=captures['setup']['document']['texture_sets'][1]
    assert min(v[0] for v in baseline)<.01 and max(v[0] for v in baseline)>.99
    inverted={'inverted','passthrough-restored','effect-on','base-inverted','saved','reopened','undo','undo-again','undo-remove','clamped'}
    errors={};uv_errors={}
    for stage,c in list(captures.items())[1:]:
        data=witness(stage);target=[[1-v for v in rgb[:3]] if stage in inverted else rgb[:3] for rgb in baseline]
        errors[stage]=max(abs(actual[k]-expected[k]) for actual,expected in zip(data['samples'],target) for k in range(3));assert errors[stage]<2e-6,(stage,errors[stage])
        assert all(abs(v-.2)<1e-6 for v in data['roughness']),stage
        assert data['sphere']==witness('setup')['sphere'] and c['document']['texture_sets'][1]==sphere,stage
        assert doc(stage)['layers'][:len(lower)]==lower,stage
        if stage not in ['setup']:assert data['paint_pages']==0,stage
        has_effect=stage not in ['setup','empty-normal','passthrough','removed']
        assert data['row_count']==len(lower)+(1 if stage!='setup' else 0)+int(has_effect),stage
        if has_effect:assert not data['paint_enabled'] and data['properties_visible'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
        assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    for stage in ['fresh-levels','inverted','normal-control','passthrough-restored','roughness-target','roughness-reset']:
        effects=doc(stage)['layers'][-1]['effects'];assert len(effects)==1 and effects[0]['type']=='levels',stage
    effect=doc('fresh-levels')['layers'][-1]['effects'][0]
    assert [effect[k] for k in ['input_low','gamma','input_high','output_low','output_high','clamp']]==[0,1,1,0,1,False]
    assert effect['channel']==0
    assert doc('roughness-target')['layers'][-1]['effects'][0]['channel']==doc('roughness-reset')['layers'][-1]['effects'][0]['channel']==2
    assert doc('roughness-reset')['layers'][-1]['effects'][0]['output_low']==0
    assert doc('clamped')['layers'][-1]['effects'][0]['clamp'] and not doc('reset-base')['layers'][-1]['effects'][0]['clamp']
    assert doc('saved')==doc('reopened')==doc('undo')==doc('undo-again')==doc('undo-remove')
    assert doc('edited')==doc('redo') and doc('removed')['layers'][-1]['effects']==[]
    for stage in ['inverted','normal-control','passthrough-restored','effect-off','effect-on']:
        a0=Image.open(a.output/'fresh-levels-viewport.png').convert('RGB');b0=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');box=(a0.width//2+60,90,a0.width-60,a0.height-60)
        uv_errors[stage]=sum(ImageStat.Stat(ImageChops.difference(a0.crop(box),b0.crop(box))).mean)/3
        assert (uv_errors[stage]>1 if stage in inverted else uv_errors[stage]<.01),(stage,uv_errors[stage])
    result['passthrough_demo']={'pid':process.pid,'stages':stages,'native_rgb_inversion_max_errors':errors,'uv_frame_differences':uv_errors,'empty_paint_layer_remains_unrasterized':True,'normal_control_restores_lower_content':True,'effect_child_visibility_removal_and_paint_disable':True,'fresh_levels_defaults_channel_retarget_reset_and_clamp':True,'lower_layers_sphere_mesh_camera_unchanged':True,'packed_save_open_edit_undo_redo_and_remove_undo':True,'scope':'PD-042 and content-side Levels foundation. Full mask-side Levels is deferred to PD-054/056; non-neutral Painter gamma and Auto equivalence are unclaimed.'}
if a.normal_vectors:
    import math,hashlib
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    stages=['initial','setup','flat-combine','flat-detail','flat-inverse','tilted-combine','tilted-detail','tilted-inverse','swapped-combine','swapped-detail','swapped-inverse','bottom-flat-combine','bottom-flat-detail','bottom-flat-inverse','top-flat-combine','top-flat-detail','top-flat-inverse','saved','reopened','edited','undo','redo','restored']
    assert list(captures)==stages
    def info(stage):return captures[stage]['normal_vectors']
    def normalize(vector):
        length=sum(x*x for x in vector)**.5
        return [x/length for x in vector] if length>1e-12 else [0,0,1]
    def rotate_detail(upper,lower):
        # Independent shortest-arc Rodrigues rotation from +Z to the lower normal.
        normal=normalize(lower);detail=normalize(upper);sine=(normal[0]**2+normal[1]**2)**.5;cosine=normal[2]
        if sine<1e-10:return detail
        axis=[-normal[1]/sine,normal[0]/sine,0];dot=sum(a0*b0 for a0,b0 in zip(axis,detail));cross=[axis[1]*detail[2]-axis[2]*detail[1],axis[2]*detail[0]-axis[0]*detail[2],axis[0]*detail[1]-axis[1]*detail[0]]
        return normalize([detail[c]*cosine+cross[c]*sine+axis[c]*dot*(1-cosine) for c in range(3)])
    def expected(data):
        upper=normalize(data['upper']);lower=normalize(data['lower'])
        if data['mode']=='Normal Map Combine':return normalize([upper[0]+lower[0],upper[1]+lower[1],upper[2]*lower[2]])
        if data['mode']=='Normal Map Detail':return rotate_detail(upper,lower)
        assert data['mode']=='Normal Map Inverse Detail';return rotate_detail(lower,upper)
    errors={};baked_separation={};first=captures['setup']
    for stage,c in list(captures.items())[1:]:
        data=c['normal_vectors'];assert data['lower_mode']=='Normal Map Detail' and c['inspection']==3,stage
        assert data['sample'][3]==1;normal=[2*x-1 for x in data['sample'][:3]];errors[stage]=max(abs(x-y) for x,y in zip(normal,expected(data)));assert errors[stage]<2e-6,(stage,normal,expected(data))
        assert data['sphere']==first['normal_vectors']['sphere'],stage
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==first['native'][key],(stage,key)
        if stage in ['saved','reopened','edited','undo','redo','restored']:
            baked_separation[stage]=max(abs(a0-b0) for a0,b0 in zip(data['complete'][:3],data['sample'][:3]));assert baked_separation[stage]>.02,stage
    for stage in ['flat-combine','flat-detail','flat-inverse']:
        assert not list((a.output/('normal-'+stage)).glob('*normal*')),stage
    assert len({tuple(round(x,6) for x in info(stage)['sample']) for stage in ['tilted-combine','tilted-detail','tilted-inverse']})==3
    assert info('tilted-combine')['sample']==info('swapped-combine')['sample']
    for a0,b0 in [('tilted-detail','swapped-inverse'),('tilted-inverse','swapped-detail')]:assert max(abs(x-y) for x,y in zip(info(a0)['sample'],info(b0)['sample']))<1e-6
    assert captures['saved']['document']==captures['reopened']['document']==captures['undo']['document']==captures['restored']['document']
    assert info('saved')['hash']==info('reopened')['hash']==info('undo')['hash']==info('restored')['hash'] and info('edited')['hash']==info('redo')['hash']!=info('saved')['hash']
    result['normal_vectors_demo']={'pid':process.pid,'stages':stages,'independent_rodrigues_native_max_errors':errors,'normal_maps_not_exported':True,'distinct_whiteout_rnm_inverse_and_swapped_order':True,'non_neutral_flat_identity':True,'authored_inspection_separate_from_baked_complete_normals':baked_separation,'new_layer_detail_default_and_preserved_lower_mode':True,'sphere_geometry_camera_unchanged':True,'packed_open_mode_edit_undo_redo':True,'scope':'Blend math is checked in the viewport sample. Normal maps are not exported.'}
if a.blend_families:
    import colorsys
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    arithmetic=['divide','inverse-divide','darken','lighten','linear-dodge','subtract','inverse-subtract','difference','exclusion','signed-addition']
    contrast=['overlay','soft-light','hard-light','vivid-light','linear-light','pin-light'];hsv=['tint','saturation','color','value'];gray=['gray-'+mode for mode in hsv]
    assert list(captures)==['initial','setup',*arithmetic,*contrast,*hsv,*gray,'saved','reopened','edit-open','undo-open','redo-open','restored']
    def info(stage):return captures[stage]['families']
    first=captures['setup'];bytes_expected=[153,255,77,128,205,0,51,51,128,78,115,108,166,153,204,127];scalar_errors={};exports={}
    for i,stage in enumerate(arithmetic+contrast):
        value=info(stage)['roughness'];assert value[3]==1;scalar_errors[stage]=abs(value[0]-bytes_expected[i]/255);assert scalar_errors[stage]<2e-7,(stage,value,bytes_expected[i])
        assert abs(info(stage)['top_roughness']-(.5 if i<10 else .75))<1e-7
        image=Image.open(a.output/('blend-'+stage)/'CubeSurface_roughness.png');assert image.mode=='RGB' and image.size==(128,128) and image.getextrema()==((bytes_expected[i],bytes_expected[i]),)*3,(stage,image.getextrema());exports[stage]=bytes_expected[i]
        reference=repo/'replica/specs/fixtures'/((f'PD-038-{[6,7,8,9,10,11,12,13,14,15][i]}-Roughness.png') if i<10 else (f'PD-039-{[16,21,22,23,24,25][i-10]}-Roughness.png'))
        witness=Image.open(reference);assert witness.getextrema()==image.getextrema(),stage
    top=colorsys.rgb_to_hsv(.2,.8,.3);bottom=colorsys.rgb_to_hsv(.7,.2,.4);hsv_errors={}
    for stage in hsv:
        expected=list(bottom)
        if stage in ['tint','color']:expected[0]=top[0]
        if stage in ['saturation','color']:expected[1]=top[1]
        if stage=='value':expected[2]=top[2]
        rgb=colorsys.hsv_to_rgb(*expected);actual=info(stage)['color'];assert actual[3]==1
        hsv_errors[stage]=max(abs(a0-b0) for a0,b0 in zip(actual[:3],rgb));assert hsv_errors[stage]<2e-6,(stage,actual,rgb)
    assert len({info(stage)['color_hash'] for stage in gray[:3]})==1 and info('gray-value')['color_hash']!=info('gray-tint')['color_hash']
    for stage in gray[:3]:assert max(abs(value-.6) for value in info(stage)['color'][:3])<1e-6
    for stage,c in list(captures.items())[1:]:
        assert c['families']['sphere']==first['families']['sphere'],stage
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==first['native'][key],(stage,key)
        layers=c['document']['texture_sets'][0]['layers'];assert len(layers)==3 and [x['id'] for x in layers]==[x['id'] for x in first['document']['texture_sets'][0]['layers']],stage
        assert layers[-1]['mask']['enabled']==False and layers[-1]['channel_opacity']==[1]*5,stage
        if stage in hsv+gray+['saved','reopened','edit-open','undo-open','redo-open','restored']:assert abs(info(stage)['roughness'][0]-127/255)<2e-7,stage
    assert captures['saved']['document']==captures['reopened']['document']==captures['undo-open']['document']==captures['restored']['document']
    assert info('saved')['color_hash']==info('reopened')['color_hash']==info('undo-open')['color_hash']==info('restored')['color_hash']
    assert info('edit-open')['color_hash']==info('redo-open')['color_hash']==info('gray-tint')['color_hash']
    result['blend_families_demo']={'pid':process.pid,'stages':list(captures),'painter_scalar_export_bytes':exports,'native_scalar_max_errors':scalar_errors,'saturated_hsv_native_max_errors':hsv_errors,'achromatic_atom_value_vs_hs_behavior':True,'channel_independence_and_unchanged_sphere_mesh_camera':True,'packed_save_open_edit_undo_redo':True,'scope':'16 native scalar/PNG witnesses match the provided Painter constants. Saturated HSV mathematics is tested against Adobe component semantics; exact chromatic Painter parity and HDR behavior are not claimed.'}
if a.tile_resolution:
    import copy
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','opened','selected-1002','override-512','selected-1001','returned','undo-size','redo-size','saved','reopened','painted','undo-paint','explicit-default','inherited-parent','restored','reset','undo-reset']
    def info(stage):return captures[stage]['tile_size']
    def tile(stage,number):return info(stage)['tiles'][str(number)]
    def document(stage):return captures[stage]['document']
    def stripped(document):
        result=copy.deepcopy(document)
        for s in result['texture_sets']:
            for key in ['resolution','height','tile_resolutions']:s.pop(key)
        return result
    first=captures['opened'];expected={'opened':(1024,1024,False),'selected-1002':(1024,1024,False),'override-512':(1024,512,True),'selected-1001':(1024,512,True),'returned':(1024,512,True),'undo-size':(1024,1024,False),'redo-size':(1024,512,True),'saved':(1024,512,True),'reopened':(1024,512,True),'painted':(1024,512,True),'undo-paint':(1024,512,True),'explicit-default':(1024,1024,True),'inherited-parent':(512,1024,True),'restored':(1024,512,True),'reset':(1024,1024,False),'undo-reset':(1024,512,True)}
    for stage,(one,two,override) in expected.items():
        data=info(stage);assert tile(stage,1001)['width']==tile(stage,1001)['height']==one and tile(stage,1002)['width']==tile(stage,1002)['height']==two,stage
        assert not tile(stage,1001)['override'] and tile(stage,1002)['override']==override,stage
        assert captures[stage]['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        navigation=captures['reopened'] if list(captures).index(stage)>=list(captures).index('reopened') else first
        for key in ['uv_center','uv_vertical_extent','uv_first','layout']:assert captures[stage]['view'][key]==navigation['view'][key],(stage,key)
        for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==first['native'][key],(stage,key)
        if stage!='painted':
            assert stripped(document(stage))==stripped(first['document']),stage
            assert data['layers']==info('opened')['layers'],stage
            if one==1024:assert tile(stage,1001)['base_hash']==tile('opened',1001)['base_hash'],stage
    assert info('selected-1002')['visible'] and info('selected-1002')['label']=='1024 (Default)' and info('selected-1002')['value']==0
    assert info('selected-1001')['selected']==1001 and info('selected-1001')['label']=='1024 (Default)'
    assert info('override-512')['selected']==info('returned')['selected']==1002 and info('returned')['label']=='512'
    assert info('explicit-default')['value']==1024 and info('explicit-default')['label']=='1024'
    assert info('inherited-parent')['parent']==512 and info('inherited-parent')['label']=='1024'
    assert info('reset')['value']==0 and info('reset')['label']=='1024 (Default)' and info('undo-reset')['value']==512
    assert document('saved')==document('reopened')==document('undo-paint')==document('restored')==document('undo-reset')
    assert info('painted')['layers']!=info('reopened')['layers'] and tile('painted',1001)['base_hash']!=tile('reopened',1001)['base_hash'] and tile('painted',1002)['base_hash']!=tile('reopened',1002)['base_hash']
    dimensions={};errors={}
    for label in ['override','reopened']:
        files=list((a.output/('maps-'+label)).glob('*.png'));assert len(files)==14,(label,len(files))
        for file in files:
            expected=(512,512) if '_1002' in file.name else (1024,1024);image=Image.open(file);assert image.size==expected,(file,image.size)
            dimensions[file.name]=image.size
    for file in (a.output/'maps-override').glob('*.png'):
        before=Image.open(file).convert('RGB');after=Image.open(a.output/'maps-reopened'/file.name).convert('RGB');errors[file.name]=sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/3;assert errors[file.name]==0,(file,errors[file.name])
    evidence={entry['export']:entry['dimensions'] for entry in info('reopened')['evidence']}
    for label,items in evidence.items():
        assert {x['tile']:x['width'] for x in items}=={1001:1024,1002:512}
        for item in items:assert item['normal_floats']==item['ao_floats']==item['width']*item['height']*4
    assert all(plane['resolution']==128 for plane in document('reopened')['texture_sets'][0]['mesh_maps'][3]['tiles'])
    result['tile_resolution_demo']={'pid':process.pid,'stages':list(captures),'actual_png_dimensions':dimensions,'native_float_buffer_dimensions':evidence,'packed_open_export_mean_byte_errors':errors,'inherited_vs_explicit_even_when_equal':True,'parent_changes_only_inherited_tiles':True,'size_undo_redo_and_reset':True,'cross_tile_brush_after_open_and_undo':True,'retained_authoring_camera_and_uv_navigation':True,'baked_ao_128_independent_of_evaluation_size':True}
if a.custom_channel:
    import copy
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','opened','added','renamed','enabled','painted3d','inspected','painted2d','undo-stroke','redo-stroke','material','srgb','undo-format','redo-format','saved','reopened','painted-open','undo-open','removed','recovered']
    def info(stage):return captures[stage]['custom']
    def document(stage):return captures[stage]['document']
    def strip_users(document):
        result=copy.deepcopy(document)
        for s in result['texture_sets']:
            s.pop('users')
            for layer in s['layers']:layer.pop('users')
        return result
    first=captures['opened']
    assert not info('opened')['present'] and info('added')['present'] and info('added')['name']=='User7' and info('added')['precision']==7 and not info('added')['brush'] and info('added')['value']==1
    for stage,c in list(captures.items())[1:]:
        assert strip_users(c['document'])==strip_users(first['document']),stage
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1],stage
        assert c['custom']['native_pbr']==first['custom']['native_pbr'],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        for field in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][field]==first['native'][field],(stage,field)
        if stage not in ['opened','added']:assert c['custom']['name']==c['custom']['property_label']=='BTS coverage',stage
    assert info('enabled')['brush'] and abs(info('enabled')['value']-.4)<1e-6
    assert info('painted3d')['pages']>0 and info('painted3d')['operations']==1 and info('painted3d')['author']==128
    assert info('painted3d')['native_hash']!=info('added')['native_hash']
    scalar_errors={}
    for stage,c in list(captures.items())[1:]:
        data=c['custom']
        if not data['present'] or not data['pages']:continue
        sample=data['native_sample'];assert sample[3]>.05,(stage,sample)
        expected=data['author_value']
        if data['precision']==0:
            encoded=12.92*expected if expected<=.0031308 else 1.055*expected**(1/2.4)-.055
            quantized=round(encoded*255)/255
            expected=quantized/12.92 if quantized<=.04045 else ((quantized+.055)/1.055)**2.4
        scalar_errors[stage]=max(abs(component/sample[3]-expected) for component in sample[:3]);assert scalar_errors[stage]<3e-6,(stage,data,expected)
    assert info('inspected')['label']=='BTS coverage' and captures['inspected']['inspection']==14
    assert info('painted2d')['hash']!=info('painted3d')['hash'] and info('painted2d')['operations']==2
    assert info('undo-stroke')['hash']==info('painted3d')['hash'] and info('redo-stroke')['hash']==info('painted2d')['hash']
    assert captures['material']['inspection']==-1
    for stage in ['srgb','redo-format','saved','reopened','painted-open','undo-open','removed','recovered']:assert info(stage)['precision']==0,stage
    assert info('undo-format')['precision']==7 and info('srgb')['hash']==info('painted2d')['hash']
    assert info('srgb')['native_hash']!=info('material')['native_hash'] and info('undo-format')['native_hash']==info('material')['native_hash']
    assert document('saved')==document('reopened')==document('undo-open')==document('recovered')
    assert info('reopened')['loaded'] and info('reopened')['operations']==2
    assert info('painted-open')['hash']!=info('reopened')['hash'] and info('undo-open')['hash']==info('reopened')['hash']
    assert not info('removed')['present'] and info('recovered')['present'] and info('removed')['hash']==info('recovered')['hash']==info('reopened')['hash']
    assert info('recovered')['native_hash']==info('reopened')['native_hash']
    result['custom_channel_demo']={'pid':process.pid,'stages':list(captures),'user7_default_l32f_disabled_brush_and_value_one':True,'native_scalar_sample_errors':scalar_errors,'inline_rename_accessible_properties_and_viewport_label':True,'actual_3d_and_uv_scalar_brush_undo_redo':True,'material_and_other_set_unchanged':True,'srgb_evaluation_changes_native_image_without_changing_authoring':True,'packed_custom_pixels_and_spatial_history_open':True,'brush_undo_after_open':True,'remove_readd_retains_name_precision_pixels':True,'scope':'Eight scalar user slots; reference User7 sequence reproduced. Independent modern color/data classification and user-channel export are not claimed.'}
if a.precision:
    import copy,struct
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    labels=['sRGB8','L8','RGB8','L16','RGB16','L16F','RGB16F','L32F','RGB32F']
    stages=['srgb8',*labels[1:]]
    assert list(captures)==['initial','opened',*stages,'metal32','undo-format','redo-format','saved','reopened','restored']
    raw=[.123456,.456789,.789123]
    def q(values,index):
        if index in [1,3,5,7]:values=[sum(v*w for v,w in zip(values,[.2126,.7152,.0722]))]*3
        if index==0:
            def srgb(v):
                encoded=12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055;q=round(encoded*255)/255
                return q/12.92 if q<=.04045 else ((q+.055)/1.055)**2.4
            return [srgb(v) for v in values]
        if index in [1,2,3,4]:maximum=255 if index in [1,2] else 65535;return [round(v*maximum)/maximum for v in values]
        if index in [5,6]:return [struct.unpack('e',struct.pack('e',v))[0] for v in values]
        return values
    first=captures['opened'];errors={}
    def stripped(document):
        result=copy.deepcopy(document)
        for item in result['texture_sets']:item.pop('precision')
        return result
    for stage,c in list(captures.items())[1:]:
        assert stripped(c['document'])==stripped(first['document']),stage
        assert c['document']['texture_sets'][1]==first['document']['texture_sets'][1],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        for field in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][field]==first['native'][field],(stage,field)
        assert all(f['choices']==9 for f in c['precision']['formats'])
    for index,stage in enumerate(stages):
        c=captures[stage];f=c['precision']['formats'][0];assert f['label']==labels[index] and f['index']==index
        pixel=c['precision']['samples'][0];assert pixel[3]==1
        errors[stage]=max(abs(v-expected) for v,expected in zip(pixel[:3],q(raw,index)));assert errors[stage]<2e-6,(stage,pixel,q(raw,index))
    def metal(stage):return captures[stage]['precision']['samples'][1][0]
    assert abs(metal('metal32')-.234567)<1e-7 and abs(metal('undo-format')-round(.234567*255)/255)<1e-7
    assert metal('redo-format')==metal('metal32')
    assert captures['saved']['document']==captures['reopened']['document']
    assert captures['saved']['precision']==captures['reopened']['precision']
    assert captures['restored']['document']==first['document']
    assert captures['restored']['precision']['hash']==captures['srgb8']['precision']['hash']
    assert len({captures[stage]['precision']['hash'] for stage in stages})>=7
    result['channel_precision_demo']={'pid':process.pid,'stages':list(captures),'ordered_formats':labels,'native_float_buffer_max_errors':errors,'integer_srgb_half_and_float32_evaluation':True,'scalar_weighted_grayscale_and_rgb_restore':True,'metallic_accepts_rgb32f_and_undo_redo':True,'retained_original_layer_values_and_pixels':True,'packed_save_open_retains_precision_and_images':True,'scope':'Evaluation quantization with full float working buffers and retained authoring rasters; does not claim reduced packed storage memory.'}
if a.recover_channel:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','opened','removed','other','returned','recovered','removed-again','saved-removed','opened-removed','recovered-open','painted','undo-paint','cube','sphere']
    def sphere(stage):return captures[stage]['document']['texture_sets'][1]
    first=captures['opened'];original=sphere('opened');assert original['ao_channel'] and original['ao_mixing']==1
    removed={'removed','other','returned','removed-again','saved-removed','opened-removed'}
    for stage,c in list(captures.items())[1:]:
        data=sphere(stage)
        assert data['ao_channel']==(stage not in removed),stage
        assert data['ao_mixing']==1,stage
        assert data['mesh_maps']==original['mesh_maps'],stage
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0],stage
        if stage!='painted':assert data['layers']==original['layers'],stage
        assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
        ao=c['ao'];control_active=stage not in removed and stage!='cube';assert ao['enabled']==control_active and ao['mode']==('Replace' if control_active else 'Multiply'),stage
        assert ao['baked_hash']==first['ao']['baked_hash'],stage
        if stage in removed:
            for sample in ao['samples']:assert abs(sample['value']-sample['baked'])<1e-6,(stage,sample)
        elif stage!='painted':assert ao['hash']==first['ao']['hash'] and ao['samples']==first['ao']['samples'],stage
    assert captures['removed']['ao']['hash']!=first['ao']['hash']
    assert captures['painted']['ao']['hash']!=first['ao']['hash']
    assert captures['saved-removed']['document']==captures['opened-removed']['document']
    def uv_image(stage):
        c=captures[stage];pane=next(p['rect'] for p in c['view']['panes'] if p['view']=='2D');x,y,w,h=pane
        return Image.open(a.output/(stage+'-viewport.png')).convert('RGB').crop((x,y+24,x+w,y+h))
    def uv_error(before,after):return sum(ImageStat.Stat(ImageChops.difference(uv_image(before),uv_image(after))).mean)/3
    removed_error=uv_error('opened','removed');recovered_error=uv_error('opened','recovered')
    assert removed_error>.05 and recovered_error<.25,(removed_error,recovered_error)
    result['recover_channel_demo']={'pid':process.pid,'stages':list(captures),'remove_readd_without_undo_retains_ao_and_replace':True,'unlit_uv_material_removed_mean_byte_difference':removed_error,'unlit_uv_material_recovered_mean_byte_error':recovered_error,'removed_output_is_retained_mesh_map':True,'save_while_removed_and_open_recovery':True,'authored_layers_geometry_camera_other_set_unchanged':True,'recovered_channel_brush_and_undo':True}
if a.rectangular:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','opened','unlinked','rectangular','other','returned','relinked','undo-link','redo-link','rect-save','saved','reopened','painted','undo-paint','portrait','square']
    def state(stage):return captures[stage]['dynamic']
    def sets(stage):return {x['name']:x for x in state(stage)['sets']}
    cube='CubeSurface';sphere='SphereSurface'
    expected={'opened':(2048,2048,True),'unlinked':(2048,2048,False),'rectangular':(2048,1024,False),'returned':(2048,1024,False),'relinked':(2048,2048,True),'undo-link':(2048,1024,False),'redo-link':(2048,2048,True),'rect-save':(2048,1024,False),'saved':(2048,1024,False),'reopened':(2048,1024,False),'painted':(2048,1024,False),'undo-paint':(2048,1024,False),'portrait':(1024,2048,False),'square':(2048,2048,True)}
    first=sets('opened')[cube]
    for stage,(width,height,linked) in expected.items():
        data=sets(stage)[cube];ui=state(stage)
        assert (data['resolution'],data['height'],data['linked'])==(width,height,linked),stage
        assert ui['selected']==str(width) and ui['selected_height']==str(height) and ui['linked']==linked and ui['height_enabled']==(not linked),stage
        assert sets(stage)[sphere]==sets('opened')[sphere] and sets(stage)[sphere]['resolution']==sets(stage)[sphere]['height']==1024,stage
        if stage!='painted':assert data['layers']==first['layers'],stage
        assert captures[stage]['view']['scene_view_matrix']==captures['opened']['view']['scene_view_matrix'],stage
        for field in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][field]==captures['opened']['native'][field],(stage,field)
    assert state('other')['selected']==state('other')['selected_height']=='1024' and not state('other')['height_enabled']
    assert captures['saved']['document']==captures['reopened']['document']==captures['undo-paint']['document']
    assert sets('painted')[cube]['layers']!=sets('reopened')[cube]['layers']
    exports={}
    for name,dimensions in [('rectangular',(2048,1024)),('reopened',(2048,1024)),('portrait',(1024,2048))]:
        files=list((a.output/('maps-'+name)).glob('*.png'));assert len(files)>=7
        for file in files:
            image=Image.open(file);assert image.size==dimensions,(file,image.size)
        exports[name]={'dimensions':dimensions,'files':[x.name for x in files]}
    errors={}
    for file in (a.output/'maps-rectangular').glob('*.png'):
        before=Image.open(file).convert('RGB');after=Image.open(a.output/'maps-reopened'/file.name).convert('RGB')
        errors[file.name]=sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/3
        assert errors[file.name]==0,(file,errors[file.name])
    # UV canvas transforms must stay normalized, independent of rectangular pixels.
    view=captures['opened']['view']
    for stage in ['unlinked','rectangular','relinked','undo-link','redo-link']:
        for key in view:
            if key.startswith('uv_'):assert captures[stage]['view'][key]==view[key],(stage,key)
    result['rectangular_demo']={'pid':process.pid,'stages':list(captures),'native_images_and_png_dimensions':exports,'packed_orm_and_infinite_padding':True,'save_open_export_byte_error':errors,'normalized_uv_canvas_and_geometry_unchanged':True,'retained_layers_other_set_and_ao':True,'link_snaps_height_to_width':True,'size_undo_redo_and_canonical_brush_undo':True}
if a.dynamic_resolution:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','opened','low','high','other','returned','undo-size','redo-size','low-save','saved','reopened','painted','undo-paint','restored','high-reopened']
    def document(stage):return captures[stage]['document']
    def state(stage):return captures[stage]['dynamic']
    def sets(stage):return {s['name']:s for s in state(stage)['sets']}
    cube='CubeSurface';sphere='SphereSurface'
    assert sets('opened')[cube]['resolution']==sets('opened')[sphere]['resolution']==1024
    expected={'low':128,'high':2048,'returned':2048,'undo-size':128,'redo-size':2048,'low-save':128,'saved':128,'reopened':128,'painted':128,'undo-paint':128,'restored':1024,'high-reopened':2048}
    for stage,resolution in expected.items():assert sets(stage)[cube]['resolution']==resolution and state(stage)['selected']==str(resolution),stage
    assert state('other')['selected']=='1024'
    for stage in list(captures)[2:]:
        assert sets(stage)[sphere]==sets('opened')[sphere],stage
        if stage!='painted':assert sets(stage)[cube]['layers']==sets('opened')[cube]['layers'],stage
        assert captures[stage]['view']['scene_view_matrix']==captures['opened']['view']['scene_view_matrix'],stage
        for field in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][field]==captures['opened']['native'][field],(stage,field)
    assert any(layer['ao']!='cbf29ce484222325' for layer in sets('opened')[sphere]['layers'])
    assert all(layer['history_loaded'] for layer in sets('reopened')[cube]['layers'])
    assert document('saved')==document('reopened')==document('undo-paint')
    assert sets('painted')[cube]['layers']!=sets('reopened')[cube]['layers']
    def maps(name):return Image.open(a.output/('maps-'+name)/'CubeSurface_color.png').convert('RGB')
    assert maps('original').size==maps('restored').size==(1024,1024) and maps('low').size==(128,128)
    assert maps('high').size==maps('high-reopened').size==(2048,2048)
    def error(a0,b0):return sum(ImageStat.Stat(ImageChops.difference(a0,b0)).mean)/3
    restored_error=error(maps('original'),maps('restored'));high_error=error(maps('high'),maps('high-reopened'));assert restored_error==high_error==0,(restored_error,high_error)
    contrasts={name:min(high-low for low,high in maps(name).getextrema()) for name in ['low','high']}
    levels={name:len(maps(name).getcolors(maps(name).width*maps(name).height)) for name in ['low','high']}
    assert min(contrasts.values())>=20 and levels['high']>levels['low'],(contrasts,levels)
    result['dynamic_resolution_demo']={'pid':process.pid,'stages':list(captures),'per_texture_set_128_1024_2048_evaluation':True,'retained_authoring_hashes_and_spatial_provenance':True,'other_set_nonzero_ao_unchanged':True,'geometry_camera_and_layers_unchanged_by_size':True,'native_channel_images_resized_without_rebaking':True,'size_undo_redo':True,'packed_low_size_open_and_canonical_brush_undo':True,'restored_original_1024_export_byte_error':restored_error,'high_before_after_packed_open_byte_error':high_error,'stroke_contrast_byte':contrasts,'distinct_coverage_levels':levels,'scope':'Retains each layer established authoring raster, with area downsampling and bilinear high evaluation. Does not claim parametric dab replay or recovery of absent bitmap detail.'}
if a.physical:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','new','assigned','custom-unit','custom-size','triplanar','internal-triplanar','saved','opened','painted','undo','tiling','assigned-tiling','obj','obj-material','source-as-cm','matched','centimeter-source','centimeter-material','custom-centimeter','authored','changed-units','undo-authoring','redo-authoring']
    def data(stage):return captures[stage]['physical']
    def document(stage):return captures[stage]['document']
    def fill(stage):return data(stage)['fill']
    for stage in list(captures)[2:13]:
        assert data(stage)['geometry_hash']==data('new')['geometry_hash'],stage
        assert captures[stage]['view']['scene_view_matrix']==captures['new']['view']['scene_view_matrix'],stage
    for stage in ['centimeter-material','custom-centimeter','authored','changed-units','undo-authoring','redo-authoring']:
        assert data(stage)['geometry_hash']==data('centimeter-source')['geometry_hash']
        assert captures[stage]['view']['scene_view_matrix']==captures['centimeter-source']['view']['scene_view_matrix'],stage
    assert data('assigned')['units']['native_per_file']==1 and data('assigned')['units']['cm_per_file']==100
    assert data('assigned')['cm_per_native']==100 and fill('assigned')['physical'] and fill('assigned')['size']==[10]*3
    assert all(abs(v-2)<1e-5 for v in fill('assigned')['effective'])
    assert data('custom-unit')['cm_per_native']==50 and all(abs(v-1)<1e-5 for v in fill('custom-unit')['effective'])
    assert fill('custom-size')['custom'] and fill('custom-size')['size']==[20]*3 and all(abs(v-.5)<1e-5 for v in fill('custom-size')['effective'])
    assert fill('triplanar')['mode']==fill('internal-triplanar')['mode']==1 and not fill('triplanar')['custom']
    assert data('triplanar')['cm_per_native']==50 and data('internal-triplanar')['cm_per_native']==100
    assert document('saved')==document('opened')==document('undo') and document('painted')!=document('opened')
    assert not data('opened')['source_exists'] and not data('painted')['source_exists']
    assert not fill('tiling')['physical'] and not fill('assigned-tiling')['physical'] and not data('assigned-tiling')['units']['auto_physical']
    assert data('obj-material')['cm_per_native']==data('matched')['cm_per_native']==1
    assert fill('obj-material')['physical'] and fill('matched')['physical']
    assert data('centimeter-material')['units']['native_per_file']==.01 and data('centimeter-material')['units']['cm_per_file']==1
    assert data('centimeter-material')['cm_per_native']==data('custom-centimeter')['cm_per_native']==100
    assert data('undo-authoring')['units']==data('redo-authoring')['units']==data('changed-units')['units']
    assert data('undo-authoring')['units']['custom_cm']==.5 and data('undo-authoring')['cm_per_native']==50
    assert fill('authored')['scale']==fill('changed-units')['scale']==fill('redo-authoring')['scale']==[2,2] and fill('undo-authoring')['scale']==[1,1]
    def image(name,channel='Base_Color'):
        return Image.open(a.output/('maps-'+name)/('DefaultMaterial_'+channel+'.png')).convert('RGB')
    def error(a0,b0):return sum(ImageStat.Stat(ImageChops.difference(a0,b0)).mean)/3
    comparisons={}
    for a0,b0 in [('tri100','opened'),('tiling','assigned-tiling'),('obj','matched'),('internal','centimeters'),('centimeters','custom-centimeter'),('unit50','undo-authoring'),('changed-units','redo-authoring')]:
        value=error(image(a0),image(b0));comparisons[a0+'='+b0]=value;assert value<.1,(a0,b0,value)
    periodic={}
    for name in ['internal','tri100','centimeters','changed-units']:
        pixels=image(name);deviation=sum(ImageStat.Stat(pixels).stddev)/3;assert deviation>20,(name,deviation)
        width,height=pixels.size;shift=width//2;value=error(pixels.crop((0,0,shift,height)),pixels.crop((shift,0,width,height)));periodic[name]=value;assert value<.15,(name,value)
    assert error(image('internal'),image('unit50'))>10
    assert error(image('unit50'),image('size20'))>10
    for name in ['unit50','size20','tri50','tri100','opened']:
        for channel in ['Metallic','Roughness','Normal','Height','Normal_DirectX']:assert error(image('internal',channel),image(name,channel))==0,(name,channel)
    result['physical_scale_demo']={'pid':process.pid,'stages':list(captures),'obj_centimeters_and_usd_meter_centimeter_metadata':True,'custom_scale_per_source_file_unit':True,'real_10cm_material_period_changes_with_geometry_camera_fixed':True,'custom_material_size_and_uv_triplanar_modes':True,'embedded_size_assignment_preference':True,'export_pixel_mean_errors_byte':comparisons,'two_repeats_half_tile_period_errors_byte':periodic,'other_material_channels_unchanged':True,'packed_open_without_source_and_further_paint_undo':True,'authoring_undo_redo_keeps_current_unit_preferences':True}
if a.usd:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','root','cancelled','scope','frame10','subdivided','variant','configuration','saved','opened','painted','undo','reimport-frame','none','no-uv-failed','unwrapped','painted-uv','undo-uv']
    def geometry(stage):return captures[stage]['usd']
    def document(stage):return captures[stage]['document']
    def settings(stage):return document(stage)['usd']
    for a0,b0 in [('root','cancelled'),('variant','configuration'),('saved','opened'),('saved','undo'),('none','no-uv-failed'),('unwrapped','undo-uv')]:
        assert document(a0)==document(b0),(a0,b0)
        for field in ['meshes','triangles','missing_uv_meshes','uv_hash','geometry_hash','minimum','maximum']:assert geometry(a0)[field]==geometry(b0)[field],(a0,b0,field)
    expected={'root':(2,3),'scope':(1,2),'frame10':(1,2),'subdivided':(1,32),'variant':(1,16),'reimport-frame':(1,2),'none':(1,1),'unwrapped':(1,32)}
    for stage,(meshes,triangles) in expected.items():
        assert (geometry(stage)['meshes'],geometry(stage)['triangles'])==(meshes,triangles),(stage,geometry(stage))
        assert geometry(stage)['missing_uv_meshes']==0
    assert settings('root')['scope']=='/' and settings('scope')['scope']=='/World/Asset'
    assert settings('frame10')['explicit_frame'] and settings('frame10')['frame']==10
    assert settings('subdivided')['subdivision']==2 and settings('variant')['subdivision']==1
    assert settings('variant')['variants']=={'/World/Asset#modelingVariant':'high'}
    assert geometry('scope')['geometry_hash']!=geometry('frame10')['geometry_hash']
    assert abs(geometry('scope')['minimum'][0]+1)<1e-5 and abs(geometry('frame10')['minimum'][0]+2)<1e-5
    assert abs(geometry('scope')['minimum'][1])<1e-5 and abs(geometry('frame10')['minimum'][1]+1)<1e-5
    assert settings('reimport-frame')==dict(scope='/World/Asset',variants={'/World/Asset#modelingVariant':'low'},subdivision=0,frame=10,explicit_frame=True)
    assert document('reimport-frame')['id']==document('saved')['id'] and document('reimport-frame')['texture_sets']==document('saved')['texture_sets']
    for stage in ['opened','painted','undo']:assert not geometry(stage)['source_exists'],stage
    for stage in ['painted','painted-uv']:assert any(layer['paint_pages'][0]>0 for layer in document(stage)['texture_sets'][0]['layers'])
    assert settings('none')['scope']=='/World/Other' and settings('none')['subdivision']==0
    assert document('unwrapped')['auto_unwrap']['enabled'] and settings('unwrapped')['subdivision']==2
    evidence=geometry('undo-uv')['evidence'];modes={e['new_mode']:e for e in evidence if 'new_mode' in e}
    assert modes[2]['frame_enabled'] and modes[3]['subdivision_enabled']
    assert not modes[5]['frame_enabled'] and not modes[7]['frame_enabled'] and not modes[7]['subdivision_enabled']
    assert any(e.get('packed_source_absent') for e in evidence) and any(e.get('missing_uv_rejected') for e in evidence)
    assert usd_source.read_text()==original,'source data must remain unchanged by selections/imports'
    result['usd_scene_demo']={'pid':process.pid,'stages':list(captures),'native_blender_usd_import_and_openusd_composition_in_process':True,'root_and_subset_scopes':True,'isolated_low_high_modeling_variants':True,'static_frame_1_10_geometry_verified':True,'subdivision_0_1_2_and_none_control_states':True,'subdivision_after_missing_uv_generation':True,'new_and_configuration_cancel_preserve_project':True,'reimport_updates_frame_with_same_stack_ids':True,'packed_open_with_source_absent_and_further_paint_undo':True,'failed_missing_uv_import_preserves_scene':True,'source_file_unchanged':True}
if a.auto_unwrap:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']}
    assert list(captures)==['initial','failed','generated','sphere','painted','undo','saved','opened','painted-open','undo-open','reimport-off','reimport-on','preserved','recomputed','repacked']
    def uv(stage):return captures[stage]['unwrap']['sets']
    def document(stage):return captures[stage]['document']
    assert document('initial')==document('failed') and uv('initial')==uv('failed')
    for stage in ['generated','sphere','undo','saved','opened','undo-open','reimport-off','reimport-on']:
        assert uv(stage)==uv('generated'),stage
        assert captures[stage]['unwrap']['missing_uv_meshes']==0
        for name,data in uv(stage).items():
            assert data['triangles']>0 and .05<data['area']<1.01,(stage,name,data['area'])
            assert all(-1e-6<=v<=1.000001 for point in data['coordinates'] for v in point)
    assert uv('preserved')==uv('initial')
    assert all(uv('recomputed')[name]['uv_hash']!=data['uv_hash'] for name,data in uv('initial').items())
    assert all(uv('repacked')[name]['uv_hash']!=data['uv_hash'] for name,data in uv('initial').items())
    for a0,b0 in [('generated','undo'),('saved','opened'),('saved','undo-open'),('saved','reimport-off'),('saved','reimport-on')]:assert document(a0)==document(b0),(a0,b0)
    assert document('painted')!=document('undo') and document('painted-open')!=document('opened')
    assert any(layer['paint_pages'][0]>0 for layer in document('painted-open')['texture_sets'][0]['layers'])
    options={'enabled':True,'seams':0,'islands':0,'packing':0,'margin':1,'hard_surface':False}
    assert document('generated')['auto_unwrap']==options and document('opened')['auto_unwrap']==options
    assert document('recomputed')['auto_unwrap']==dict(enabled=True,seams=1,islands=1,packing=1,margin=3,hard_surface=False)
    assert document('repacked')['auto_unwrap']==dict(enabled=True,seams=0,islands=0,packing=1,margin=2,hard_surface=False)
    evidence=captures['repacked']['unwrap']['evidence'];assert evidence[0]['missing_off_rejected'] and evidence[0]['document_retained'] and 'UV' in evidence[0]['error']
    assert all(e['options_default'] and e['off_options_enabled'] for e in evidence if 'options_default' in e)
    assert all(e['default_off'] for e in evidence if 'default_off' in e)
    attempts=[e for e in evidence if 'reimport' in e];assert len(attempts)==2 and attempts[0]['error'] and not attempts[1]['error']
    overlaps={}
    for name,data in uv('generated').items():
        occupied=set();overlap=0;coords=data['coordinates'];size=128
        for i in range(0,len(coords),3):
            a0,b0,c0=coords[i:i+3];det=(b0[0]-a0[0])*(c0[1]-a0[1])-(b0[1]-a0[1])*(c0[0]-a0[0])
            if abs(det)<1e-14:continue
            for y in range(max(0,int(min(a0[1],b0[1],c0[1])*size)),min(size,int(max(a0[1],b0[1],c0[1])*size)+1)):
                for x in range(max(0,int(min(a0[0],b0[0],c0[0])*size)),min(size,int(max(a0[0],b0[0],c0[0])*size)+1)):
                    px,py=(x+.5)/size-a0[0],(y+.5)/size-a0[1]
                    u=(px*(c0[1]-a0[1])-py*(c0[0]-a0[0]))/det;v=((b0[0]-a0[0])*py-(b0[1]-a0[1])*px)/det
                    if min(u,v,1-u-v)<=1e-5:continue
                    overlap+=(x,y) in occupied;occupied.add((x,y))
        assert overlap==0,(name,overlap);overlaps[name]=dict(sampled=len(occupied),overlap=overlap)
    areas={name:data['area'] for name,data in uv('generated').items()}
    result['auto_unwrap_demo']={'pid':process.pid,'stages':list(captures),'native_smart_project_angle_based_and_packing':True,'missing_uv_off_rejects_transactionally':True,'authored_uv_off_preserved_exactly':True,'generated_area_by_set':areas,'generated_nonoverlap_sampling':overlaps,'recompute_and_packing_only_modes':True,'new_options_default_off_and_accessible':True,'packed_open_and_further_paint_undo':True,'failed_reimport_retains_project_then_enabled_reimport_succeeds':True}
if a.cameras:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','excluded','imported','front','angle','uv-zoom','navigated','back','configuration','reimport-off','reimport-on','front-again','saved','opened','selected-open','painted','undo']
    def cameras(stage):return captures[stage]['cameras']
    assert not cameras('excluded')['native'] and not captures['excluded']['document']['import_cameras']
    imported=cameras('imported')['native'];assert [c['name'] for c in imported]==['FrontReference','AngleReference'],imported
    for cam,pos,fov,aspect in zip(imported,[[0,-4,0],[3,-4,1]],[45,35],[1.5,16/9]):
        assert max(abs(a-b) for a,b in zip(cam['position'],pos))<1e-5 and abs(cam['fov']-fov)<1e-4 and abs(cam['aspect']-aspect)<1e-5,cam
    assert cameras('imported')['state']['selected']=='Default camera'
    for stage in ['front','front-again','selected-open']:assert cameras(stage)['state']['selected']=='FrontReference'
    for stage in ['angle','uv-zoom','back','configuration']:assert cameras(stage)['state']['selected']=='AngleReference'
    assert cameras('navigated')['state']['selected']=='Default camera' and cameras('navigated')['state']['back_visible']
    for stage in ['front','angle','uv-zoom','navigated','back','configuration']:assert cameras(stage)['native']==imported and captures[stage]['document']==captures['imported']['document'],stage
    def matrix(stage,key):return captures[stage]['view'][key]
    assert matrix('front','scene_view_matrix')!=matrix('angle','scene_view_matrix')
    assert matrix('angle','scene_view_matrix')!=matrix('navigated','scene_view_matrix')
    assert max(abs(a-b) for a,b in zip(matrix('angle','scene_view_matrix'),matrix('back','scene_view_matrix')))<1e-5
    for stage in ['front','angle']:
        for key in ['uv_center','uv_vertical_extent']:assert matrix(stage,key)==matrix('imported',key)
    assert not cameras('reimport-off')['native'] and not captures['reimport-off']['document']['import_cameras']
    off=dict(captures['imported']['document']);off['import_cameras']=False;assert captures['reimport-off']['document']==off
    for stage in ['reimport-on','front-again','saved','opened','selected-open','undo']:
        assert captures[stage]['document']==captures['imported']['document'],stage
        assert cameras(stage)['native']==imported,stage
    assert captures['painted']['document']!=captures['imported']['document']
    evidence=cameras('reimport-on')['evidence'];assert all(e['pbr_default_on'] for e in evidence if e['stage'].startswith('new-'))
    assert all(e['disabled_before_mesh'] for e in evidence if e['stage'].startswith('configuration') or e['stage'].startswith('reimport'))
    assert all(e['enabled_after_mesh'] for e in evidence if e['stage'].startswith('reimport'))
    for stage,c in list(captures.items())[2:]:assert cameras(stage)['environment']==cameras('imported')['environment'],stage
    result['camera_import_demo']={'pid':process.pid,'stages':list(captures),'native_imported_cameras':imported,'in_process_assimp_geometry_bridge_and_native_blender_camera_ids':True,'camera_import_on_off_and_pbr_default_on':True,'scene_cameras_immutable_during_view_selection_and_navigation':True,'scroll_detaches_default_and_back_restores_pose':True,'uv_view_and_document_retained_by_camera_changes':True,'mesh_reimport_replaces_only_marked_imported_cameras':True,'packed_open_preserves_camera_data':True,'real_3d_stroke_through_selected_camera_and_undo':True}
if a.fragment_tangent:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','source','cancelled','fragment','retained','baked','external','saved','opened','vertex','fragment-again','painted','undo']
    def data(stage):return captures[stage]['tangents']
    base=captures['source']['document'];fragment=dict(base);fragment['tangent_per_fragment']=True
    assert not base['tangent_per_fragment'] and captures['cancelled']['document']==base
    for stage in ['fragment','retained']:assert captures[stage]['document']==fragment,stage
    for stage in ['source','cancelled','fragment','retained']:
        assert not any(map for st in captures[stage]['document']['texture_sets'] for map in st['mesh_maps'])
    assert data('source')['normal_hash']==data('fragment')['normal_hash']
    assert all(not s['fragment'] and s['failed']==0 for s in data('source')['shaders'].values())
    for stage in ['fragment','retained','baked','external','saved','opened','fragment-again','painted','undo']:
        assert all(s['fragment'] and s['failed']==0 for s in data(stage)['shaders'].values()),(stage,data(stage)['shaders'])
        assert data(stage)['shaders']['SphereSurface']['success']>0,(stage,data(stage)['shaders'])
    assert all(not s['fragment'] for s in data('vertex')['shaders'].values())
    source=Image.open(a.output/'source-viewport.png').convert('RGB');shaded=Image.open(a.output/'fragment-viewport.png').convert('RGB');w,h=source.size;roi=(0,0,int(w*.33),h);difference=ImageChops.difference(source.crop(roi),shaded.crop(roi)).convert('L');changed=sum(v>3 for v in difference.getdata());control=ImageChops.difference(source.crop(roi),Image.open(a.output/'cancelled-viewport.png').convert('RGB').crop(roi)).convert('L');control_pixels=sum(v>3 for v in control.getdata());assert changed>max(30,control_pixels*5),(changed,control_pixels)
    for st in captures['baked']['document']['texture_sets']:assert all(st['mesh_maps'])
    for stage in ['saved','opened','fragment-again','undo']:assert captures[stage]['document']==captures['external']['document'],stage
    vertex=dict(captures['external']['document']);vertex['tangent_per_fragment']=False;assert captures['vertex']['document']==vertex
    assert captures['painted']['document']!=captures['external']['document']
    evidence=data('external')['evidence'];assert any(e.get('stage')=='low-as-high-bake' and e['done']==e['total']==14 for e in evidence);assert any(e.get('stage')=='external-bake' and e['done']==e['total']==2 for e in evidence)
    neutral=data('external')['baked_normals'];assert len(neutral)==2
    for name,n in neutral.items():assert n['covered']>1000 and n['neutral_max_error']<.025,(name,n)
    for stage in ['cancelled','fragment','retained','baked','external']:assert captures[stage]['view']['scene_view_matrix']==captures['source']['view']['scene_view_matrix']
    for stage,c in list(captures.items())[1:]:assert data(stage)['environment']==data('source')['environment']
    log=(a.output/'run.log').read_text();assert 'Shader compilation failed' not in log and 'GPUShader: error' not in log
    result['fragment_tangent_demo']={'pid':process.pid,'stages':list(captures),'real_native_derivative_shader_changes_lighting_with_identical_normal_pixels':True,'sphere_roi_changed_pixels_above_3byte':changed,'unchanged_mode_control_pixels':control_pixels,'native_material_shader_compilation_success':True,'setting_cancel_and_reopen_retained':True,'no_automatic_bake':True,'explicit_low_as_high_jobs':14,'external_matched_geometry_fragment_basis_jobs':2,'external_neutral_normal_errors':neutral,'same_layers_and_authoring_data':True,'packed_open_and_further_paint_undo':True,'camera_and_environment_retained':True}
if a.normal_format:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','source','cancelled','directx','undo-dx','redo-dx','export-dx','opengl','export-gl','saved','opened','painted','undo']
    def author(stage):
        doc=dict(captures[stage]['document']);doc.pop('export_configuration',None);return doc
    base=author('source');directx=dict(base);directx['normal_format']='DirectX'
    assert author('cancelled')==base
    for stage in ['directx','redo-dx','export-dx']:assert author(stage)==directx,stage
    for stage in ['opengl','export-gl','saved','opened','undo']:assert author(stage)==base,stage
    assert author('undo-dx')['normal_format']=='DirectX' and author('undo-dx')!=directx
    assert author('painted')!=base
    for stage,c in list(captures.items())[1:]:
        assert c['normal_format']['environment']==captures['source']['normal_format']['environment']
        if stage not in ['opened','painted','undo']:assert c['view']['scene_view_matrix']==captures['source']['view']['scene_view_matrix'],stage
    for item in captures['opengl']['normal_format']['evidence']:assert not item['preserve_enabled']
    assert not list((a.output/'directx').glob('*normal*')) and not list((a.output/'opengl').glob('*normal*'))
    bump_dx=Image.open(a.output/'directx/CubeSurface_bump.png');bump_gl=Image.open(a.output/'opengl/CubeSurface_bump.png');assert bump_dx.size==bump_gl.size==(1024,1024) and bump_dx.mode==bump_gl.mode=='L'
    assert ImageChops.difference(bump_dx,bump_gl).getbbox() is None
    result['normal_convention_demo']={'pid':process.pid,'stages':list(captures),'format_changes_without_mesh_replacement':True,'cancel_and_authoring_pixels_preserved':True,'earlier_paint_undo_redo_retains_new_project_convention':True,'normal_maps_not_exported':True,'bump_grayscale_matches_across_conventions':True,'camera_and_environment_retained':True,'packed_save_open_and_further_paint_undo':True}
if a.project_template or a.template_load:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};expected=['initial','custom','settings','saved','opened','painted','undo'] if a.template_load else ['initial','source','cancel-save','saved-template','cancel-new','builtin','custom','settings','saved','opened','painted','undo'];assert list(captures)==expected
    template_file=a.template_load or a.output/'custom.btst';data=json.loads(template_file.read_text());assert data['format']=='bts-project-template' and data['version']==1
    import base64
    resource=data['resource'];assert hashlib.sha256(base64.b64decode(resource['data'])).hexdigest()==resource['sha256'];assert not (a.output/'template-source.hdr').exists()
    for profile in data['defaults']['texture_sets']:
        assert not profile['disabled'] and len(profile['layers'])==1 and profile['layers'][0]['kind']=='paint' and not profile['layers'][0]['mask'] and not any(profile['mesh_maps'])
        assert all(not any(tile['pages']) for tile in profile['layers'][0]['paint_tiles'])
    custom=captures['custom'];sets={s['name']:s for s in custom['document']['texture_sets']};cube=sets['CubeSurface'];assert cube['ao_channel'] and not cube['channels'][1] and not cube['padding_3d'] and cube['normal_mixing']==1 and cube['bump_method']==1
    assert custom['document']['resolution']==(512 if a.template_load else data['defaults']['resolution'])
    assert custom['document']['normal_format']==data['defaults']['normal_format']
    for current in sets.values():assert len(current['layers'])==1 and not any(current['mesh_maps']) and all(not any(t['pages']) for t in current['layers'][0]['paint_tiles'])
    actual=custom['template']['environment'];wanted=data['environment'];assert actual['resource']==resource['name'] and actual['alignment']==wanted['alignment']
    for key in ['rotation','exposure','opacity','blur']:assert abs(actual[key]-wanted[key])<.0001,(key,actual,wanted)
    assert custom['template']['color']['view_transform']==data['display']['view_transform']
    assert custom['template']['baking']==data['baking']
    for stage in ['settings','saved','opened','undo']:assert captures[stage]['document']==custom['document'],stage
    assert captures['painted']['document']!=custom['document']
    evidence=captures['settings']['template']['evidence'];bake=evidence[-1];assert bake['stage']=='bake-ui'
    for key in ['resolution','height','locked','rays','sampling','dilation']:assert bake[key]==data['baking'][key]
    if not a.template_load:
        source=captures['source'];assert any(any(t['pages']) for st in source['document']['texture_sets'] for l in st['layers'] for t in l['paint_tiles'])
        for stage in ['cancel-save','saved-template','cancel-new']:
            assert captures[stage]['document']==source['document'] and captures[stage]['template']['title']==source['template']['title'] and captures[stage]['template']['path']==source['template']['path'] and captures[stage]['template']['dirty']==source['template']['dirty']
        builtin=captures['builtin']['document'];assert builtin['normal_format']=='DirectX' and builtin['resolution']==1024
        for st in builtin['texture_sets']:assert st['channels']==[True]*5 and not st['ao_channel'] and len(st['layers'])==1 and not any(st['mesh_maps']) and all(not any(t['pages']) for t in st['layers'][0]['paint_tiles'])
        assert all(e['normal']=='DirectX' for e in evidence if e.get('kind')=='pbr')
    result['project_templates_demo']={'pid':process.pid,'stages':list(captures),'fresh_process_file_load':bool(a.template_load),'template_file':str(template_file),'packed_environment_sha256':resource['sha256'],'source_document_retained_on_save_and_cancel':not a.template_load,'builtin_pbr_directx_blank_defaults':not a.template_load,'custom_channel_display_environment_and_common_bake_defaults_restored':True,'no_geometry_paint_layers_or_mesh_maps_carried':True,'dialog_controls_match_baking_defaults':True,'packed_project_roundtrip_and_further_paint_undo':True}
if a.reassign:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','loaded','renamed','cancelled','applied','saved','reopened','painted','undo-paint']
    def sets(stage):return {s['name']:s for s in captures[stage]['document']['texture_sets']}
    old=sets('loaded')['CubeSurface'];renamed=sets('renamed');applied=sets('applied')
    assert len(renamed)==3 and renamed['CubeSurface']['disabled'] and not renamed['CompanionRenamed']['disabled']
    assert renamed['CubeSurface']['layers']==old['layers'] and renamed['SphereSurface']==sets('loaded')['SphereSurface']
    assert captures['renamed']['document']==captures['cancelled']['document']
    assert not applied['CubeSurface']['disabled'] and applied['CompanionRenamed']['disabled']
    assert applied['CubeSurface']['layers']==old['layers'] and applied['CubeSurface']['id']==old['id']
    assert applied['SphereSurface']==sets('loaded')['SphereSurface']
    for stage in ['saved','reopened','undo-paint']:assert captures[stage]['document']==captures['applied']['document'],stage
    assert captures['painted']['document']!=captures['applied']['document']
    for stage in ['renamed','cancelled','applied']:assert captures[stage]['view']['scene_view_matrix']==captures['loaded']['view']['scene_view_matrix']
    for stage,c in list(captures.items())[1:]:assert c['reassignment']['environment']==captures['loaded']['reassignment']['environment']
    evidence=captures['applied']['reassignment']['dialog'];assert len(evidence)==8
    for start in [0,4]:
        initial,swap,undo,redo=evidence[start:start+4]
        assert not initial['undo'] and not initial['redo']
        assert swap['undo'] and not swap['redo'] and swap['assigned']==redo['assigned']
        assert not undo['undo'] and undo['redo'] and undo['assigned']==initial['assigned']
        assert all(x['document']==captures['renamed']['document'] for x in [initial,swap,undo,redo])
    before=Image.open(a.output/'loaded-viewport.png').convert('RGB');after=Image.open(a.output/'applied-viewport.png').convert('RGB');empty=Image.open(a.output/'renamed-viewport.png').convert('RGB')
    restored_error=sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/(3*255);assert restored_error<.025,restored_error
    changed=sum(v>8 for v in ImageChops.difference(empty,after).convert('L').getdata());assert changed>50,changed
    result['texture_set_reassignment_demo']={'pid':process.pid,'stages':list(captures),'renamed_material_creates_empty_set_and_disables_original_without_data_loss':True,'synthetic_qt_drop_restores_original_stack':True,'dialog_undo_redo_cancel_are_transactional':True,'apply_retains_set_and_layer_ids':True,'disabled_set_and_active_bindings_survive_packed_open':True,'further_paint_undo':True,'camera_environment_and_other_set_retained':True,'restored_viewport_mean_error':restored_error,'restored_pixels_above_8byte':changed}
if a.uv_reproject:
    from PIL import ImageOps
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','spatial','uv','saved','opened','on','channel','material','saved-on','opened-on','painted-open','undo-open','original','off','material-off','restored']
    def data(stage):return captures[stage]['reprojection']
    assert data('spatial')['spatial']==[True] and data('uv')['spatial']==[True,False]
    before=Image.open(a.output/'uv-spatial-alpha.png');after=Image.open(a.output/'on-spatial-alpha.png');expected=ImageOps.mirror(before);difference=ImageStat.Stat(ImageChops.difference(after,expected)).mean[0];assert difference<2,difference
    assert sum(after.getdata())>1000 and data('on')['ink_hashes'][0]!=data('uv')['ink_hashes'][0]
    assert data('on')['ink_hashes'][1]==data('uv')['ink_hashes'][1] and data('on')['spatial']==[True,False]
    assert data('off')['ink_hashes']==data('on')['ink_hashes'],'Identical-bounds Preserve off must still reproject'
    for stage in ['channel','material','saved-on','opened-on','undo-open','off','material-off']:assert captures[stage]['document']==captures['on']['document'],stage
    for stage in ['saved','opened','original','restored']:assert captures[stage]['document']==captures['uv']['document'],stage
    assert captures['painted-open']['document']!=captures['on']['document']
    for stage,c in list(captures.items())[1:]:assert data(stage)['environment']==data('uv')['environment']
    old_mask=Image.open(a.output/'uv-uv-mask.png');new_mask=Image.open(a.output/'on-uv-mask.png');uv=Image.open(a.output/'uv-uv-alpha.png');assert ImageChops.difference(old_mask,new_mask).getbbox() is not None
    old_visible=sum(a for a,m in zip(uv.getdata(),old_mask.getdata()) if m);new_visible=sum(a for a,m in zip(uv.getdata(),new_mask.getdata()) if m);assert old_visible>1000 and new_visible<old_visible*.05,(old_visible,new_visible)
    for stage in ['on','channel','material']:assert captures[stage]['view']['scene_view_matrix']==captures['opened']['view']['scene_view_matrix']
    result['uv_reprojection_demo']={'pid':process.pid,'stages':list(captures),'recorded_3d_stroke_moves_with_mirrored_uvs':True,'spatial_mirror_mean_byte_error':difference,'uv_origin_ink_hash_is_unchanged_and_leaves_new_islands':True,'uv_visible_alpha_before':old_visible,'uv_visible_alpha_after':new_visible,'preserve_on_off_identical_for_unchanged_bounds':True,'packed_history_roundtrip_and_further_paint_undo':True,'environment_and_reimport_camera_preserved':True}
if a.reimport:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','authored','cancelled','bad-file','bad-material','reimported','sphere','material','saved','reopened','painted','undo'];baseline=captures['authored'];after=captures['reimported']
    for stage in ['cancelled','bad-file','bad-material']:
        assert captures[stage]['document']==baseline['document'] and captures[stage]['native']==baseline['native']
        assert captures[stage]['reimport']['colors']==baseline['reimport']['colors']
    expected=dict(baseline['document']);expected['source_mesh']=str(a.replacement)
    for stage in ['reimported','sphere','material','saved','reopened','undo']:assert captures[stage]['document']==expected,stage
    assert captures['painted']['document']!=expected
    assert after['reimport']['colors'][0]!=baseline['reimport']['colors'][0] and after['reimport']['colors'][1]==baseline['reimport']['colors'][1]
    assert not after['reimport']['undo_enabled'] and after['reimport']['selected']==0
    for stage,c in list(captures.items())[1:]:
        assert c['reimport']['environment']==baseline['reimport']['environment'] and c['reimport']['profile']==baseline['reimport']['profile'],stage
        assert c['native']['mesh_objects']==2
        if stage not in ['reopened','painted','undo']:assert c['view']['scene_view_matrix']==baseline['view']['scene_view_matrix'],stage
    assert captures['reopened']['reimport']['colors']==after['reimport']['colors']
    # Actual native bounds establish changed geometry; camera matrices establish no auto-frame.
    assert after['native']['bounds_maximum']!=baseline['native']['bounds_maximum']
    result['mesh_reimport_demo']={'pid':process.pid,'stages':list(captures),'staged_cancel_and_invalid_sources_preserve_entire_project':True,'same_native_material_ids_texture_set_layer_mask_map_resources_retained':True,'native_geometry_changed_without_reframing_or_environment_reset':True,'triplanar_fill_recomputed_but_uv_paint_hash_preserved':True,'geometry_change_clears_authoring_history':True,'packed_blend_roundtrip_and_continued_paint_undo':True}
def export_material_state(capture):
    doc=dict(capture['document']);doc.pop('export_configuration',None);return doc
if a.export_settings:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','loaded','saved-settings','retained','cancelled','cancel-retained','saved','reopened','project-settings','painted','changed-settings','undo-paint','undo-settings','other-project','returned','final-settings'];baseline=captures['loaded'];saved=captures['saved-settings']['document']['export_configuration']
    def config(stage):return captures[stage]['document']['export_configuration']
    assert baseline['document']['schema']>=15 and not config('loaded')
    assert saved['format']==1 and saved['size']==512 and saved['padding']==4 and saved['padding_width']==16 and len(saved['selected'])==1 and len(saved['presets'])==1
    assert saved['presets'][0]['packed']=={'name':'$mesh/$textureSet_saved','sources':[4,6,5]} and not saved['presets'][0]['standard_outputs']
    for stage in ['retained','cancelled','cancel-retained','saved','reopened','project-settings','painted','returned','final-settings']:assert config(stage)==saved,stage
    for stage in ['changed-settings','undo-paint','undo-settings']:
        expected=dict(saved);expected['size']=256;assert config(stage)==expected,stage
    assert not config('other-project')
    for stage in list(captures)[2:]:
        if stage not in ['painted','changed-settings']:assert export_material_state(captures[stage])==export_material_state(baseline),stage
    assert export_material_state(captures['painted'])!=export_material_state(baseline)
    for stage in ['saved-settings','retained','cancelled','cancel-retained','saved']:assert captures[stage]['view']['scene_view_matrix']==baseline['view']['scene_view_matrix']
    evidence=captures['final-settings']['exports'];assert len(evidence)==9
    for entry in evidence:
        assert entry['template_save_disabled'] and not entry['progress_visible']
        if entry['stage']=='other-project':assert entry['preset_count']==1 and entry['size']==0 and len(entry['selected'])==2;continue
        if entry['operation']==2:assert entry['preset_count']==3 and entry['size']==1024;continue
        assert entry['preset_count']==2 and entry['preset']=='BTS saved packed' and entry['precision']=='16 bits' and entry['padding']=='Dilation + diffusion' and entry['selected']==['CubeSurface']
        assert entry['packed_name']=='$mesh/$textureSet_saved' and [entry['red'],entry['green'],entry['blue']]==['Height','Constant 1','Unassigned (0)']
        assert entry['size']==(256 if entry['stage'] in ['changed-settings','undo-settings'] else 512)
    assert not list((a.output/'settings-only').iterdir()),'Save settings must not export or create token subfolders'
    result['export_settings_demo']={'pid':process.pid,'stages':list(captures),'save_settings_no_export_and_cancel_rolls_back_fields_and_templates':True,'schema15_packed_blend_roundtrip_embeds_custom_preset_and_components':True,'selection_directory_format_size_padding_restored':True,'separate_projects_do_not_share_configuration':True,'later_settings_survive_stroke_undo':True,'template_tab_disables_save_settings':True,'material_and_camera_unchanged_by_dialog_edits':True,'own_output_directory_remains_empty':True,'dialog_evidence':evidence}
if a.export_padding:
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','loaded','none','infinite','transparent','default','diffusion','retained'];baseline=captures['loaded']
    for c in list(captures.values())[2:]:assert export_material_state(c)==export_material_state(baseline) and c['view']['scene_view_matrix']==baseline['view']['scene_view_matrix']
    images={mode:Image.open(a.output/mode/'CubeSurface_color.png') for mode in ['none','infinite','transparent','default','diffusion']};mask=Image.open(a.output/'uv-mask.png').resize((256,256),Image.Resampling.BOX);inside=[v==255 for v in mask.getdata()];changes={};interior_errors={}
    original=list(images['none'].convert('RGB').getdata())
    for mode,image in images.items():
        assert image.size==(256,256) and image.mode==('RGBA' if mode=='transparent' else 'RGB'),(mode,image.mode)
        pixels=list(image.convert('RGB').getdata());changes[mode]=sum(a!=b for a,b in zip(original,pixels));interior_errors[mode]=max(max(abs(a[c]-b[c]) for c in range(3)) for a,b,valid in zip(original,pixels,inside) if valid);assert interior_errors[mode]<=1,(mode,interior_errors[mode])
        record=captures[mode]['exports'][-1];assert record['count']=='4 textures selected' and len(record['files'])==4 and 'Export successfully finished' in record['console'] and not any('normal' in name for name in record['files'])
    alpha=list(images['transparent'].getchannel('A').getdata());assert alpha.count(0)>1000 and alpha.count(255)>1000 and changes['infinite']>100 and changes['default']>100 and changes['diffusion']>100
    distant=next(i for i,a in enumerate(alpha) if a==0 and images['diffusion'].convert('RGB').getdata()[i]!=(0,0,0))
    assert list(images['default'].getdata())[distant]!=list(images['diffusion'].getdata())[distant]
    retained=captures['retained']['exports'][-2];assert retained=={'padding':'Dilation + diffusion','width':16,'width_enabled':True},retained
    result['export_padding_demo']={'pid':process.pid,'stages':list(captures),'five_modes_real_native_geometry_uv_mask':True,'finite_dilation_transparent_alpha_default_background_and_diffusion':True,'interior_rgb_max_error':interior_errors,'changed_pixels_vs_none':changes,'transparent_alpha_zero':alpha.count(0),'transparent_alpha_opaque':alpha.count(255),'all_thirty_pngs_selected_set_only':True,'width_disabled_for_none_and_infinite':True,'document_and_camera_unchanged':True,'retained':retained}
if a.export_packing:
    import runpy
    readers=runpy.run_path(str(repo/'scripts/host/export-file-check.py'));demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};assert list(captures)==['initial','loaded','reference','orm','remapped','constants','retained'];baseline=captures['loaded']
    for c in list(captures.values())[2:]:assert export_material_state(c)==export_material_state(baseline) and c['view']['scene_view_matrix']==baseline['view']['scene_view_matrix']
    source={name:Image.open(a.output/'reference'/f'SphereSurface_{name.lower()}.png').convert('L') for name in ['Mixed_AO','Roughness','Metallic']}
    orm=Image.open(a.output/'orm/SphereSurface_orm.png').convert('RGB');expected=Image.merge('RGB',(source['Mixed_AO'],source['Roughness'],source['Metallic']));assert ImageChops.difference(orm,expected).getbbox() is None
    remap=Image.open(a.output/'remapped/SphereSurface_mro.png').convert('RGB');expected=Image.merge('RGB',(source['Metallic'],source['Mixed_AO'],source['Roughness']));assert ImageChops.difference(remap,expected).getbbox() is None
    assert source['Mixed_AO'].getextrema()[0]<source['Mixed_AO'].getextrema()[1]
    constants=readers['exr_pixels'](a.output/'constants/SphereSurface_test.exr');assert set(constants['types'].values())=={2} and constants['sample'](512,512)==[-.125,1,0]
    for stage in ['orm','remapped','constants']:
        record=captures[stage]['exports'][-1];assert record['count']=='1 textures selected' and len(record['files'])==1 and 'Export successfully finished' in record['console']
    retained=captures['retained']['exports'][-2];assert retained=={'packed_name':'$textureSet_test','red':'Bump','green':'Constant 1','blue':'Unassigned (0)'},retained
    result['export_packing_demo']={'pid':process.pid,'stages':list(captures),'independent_rgb_sources_selected_in_real_template_controls':True,'orm_exactly_matches_three_independent_reference_exports_all_pixels':True,'remapped_mro_exactly_matches_swapped_sources_all_pixels':True,'ao_reference_is_nonuniform':True,'float32_packed_signed_height_constant_one_empty_zero_verified':True,'one_output_per_packed_preset':True,'document_and_camera_unchanged':True,'retained':retained}
if a.export_size:
    import runpy
    readers=runpy.run_path(str(repo/'scripts/host/export-file-check.py'));demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};baseline=captures['loaded'];details=[]
    for stage in ['png8','png16','exr16','exr32','retained']:
        assert export_material_state(captures[stage])==export_material_state(baseline) and captures[stage]['view']['scene_view_matrix']==baseline['view']['scene_view_matrix'],stage
    sizes={'png8':{'CubeSurface':1024,'SphereSurface':512},'png16':{'CubeSurface':256,'SphereSurface':512},'exr16':{'CubeSurface':256,'SphereSurface':256},'exr32':{'CubeSurface':1024,'SphereSurface':1024}}
    for encoding,expected in sizes.items():
        record=captures[encoding]['exports'][-1];assert record['count']=='10 textures selected' and len(record['files'])==10 and not any('normal' in name for name in record['files'])
        assert 'Export successfully finished' in record['console']
        measured={}
        for name in record['files']:
            # Header checks for every output, raw samples for the base colors.
            path=a.output/encoding/name;set_name=name.split('_')[0];size=expected[set_name]
            if encoding.startswith('png'):
                import struct
                header=path.read_bytes()[:33];width,height,bits=struct.unpack('>IIB',header[16:25]);assert width==height==size and bits==(8 if encoding=='png8' else 16)
            else:
                data=readers['exr_pixels'](path);assert data['width']==data['height']==size and set(data['types'].values())=={1 if encoding=='exr16' else 2}
            if name.startswith('SphereSurface_') and any('_'+component+'.' in name for component in ['roughness','bump']):
                data=readers['png_pixels'](path) if encoding.startswith('png') else data
                value=data['sample'](size//2,size//2);tolerance={'png8':1/255,'png16':1/65535,'exr16':.0006,'exr32':1e-6}[encoding]
                expected_values=[.123456] if '_roughness.' in name else [0 if encoding.startswith('png') else -.125]
                assert all(abs(c-v)<tolerance for c,v in zip(value,expected_values)),(encoding,name,value,expected_values)
            measured[set_name]=size
        details.append({'encoding':encoding,'sizes':measured,'files':len(record['files'])})
    retained=captures['retained']['exports'][-2];assert retained=={'local_override':False,'inherited_size':1024,'local_size_disabled':True,'row':'SphereSurface'},retained
    result['export_size_demo']={'pid':process.pid,'stages':list(captures),'global_changes_do_not_override_explicit_set_size':True,'disabled_override_restores_inheritance_and_removes_star':True,'included_sets_independent_of_editing_selection':True,'native_box_resampling':True,'all_56_output_dimensions_and_precision_verified':details,'document_resolution_layers_and_camera_unchanged':True,'retained':retained}
if a.export_naming:
    import runpy
    readers=runpy.run_path(str(repo/'scripts/host/export-file-check.py'))
    demo=json.loads((a.output/'paint.json').read_text());captures={c['stage']:c for c in demo['captures']};baseline=captures['loaded'];evidence=captures['retained']['exports'];assert len(evidence)==6
    for c in list(captures.values())[2:]:
        assert export_material_state(c)==export_material_state(baseline) and c['view']['scene_view_matrix']==baseline['view']['scene_view_matrix']
    details=[]
    for encoding in ['png8','png16','exr16','exr32']:
        record=captures[encoding]['exports'][-1];extension='png' if encoding.startswith('png') else 'exr';nested=f'two_materials/SphereSurface_BaseColor.{extension}'
        assert len(record['files'])==6 and nested in record['files'] and not any('normal' in name for name in record['files']),record
        assert all(n.startswith('two_materials_SphereSurface_') or n==nested for n in record['files'])
        assert 'Export successfully finished' in record['console']
        path=a.output/encoding/nested;data=readers['png_pixels' if extension=='png' else 'exr_pixels'](path);assert data['width']==data['height']==1024
        details.append({'encoding':encoding,'nested':nested,'center':data['sample'](512,512)})
        assert not list((a.output/encoding).glob('.bts-*'))
    rejection=evidence[-2];assert rejection['restored'] and rejection['preset']=='BTS_reference_tokens';assert 'Unknown' in rejection['unknown_rejected'] and 'duplicate' in rejection['collision_rejected'] and 'Invalid' in rejection['traversal_rejected']
    result['export_naming_demo']={'pid':process.pid,'stages':list(captures),'duplicated_renamed_template_requires_explicit_selection':True,'mesh_texture_set_channel_tokens_and_real_subfolder':True,'four_encodings_with_predictable_names':details,'unknown_token_collision_parent_traversal_rejected_in_preview':rejection,'document_and_camera_unchanged':True,'temporary_files_removed':True}
if a.texture_export:
    import runpy
    readers=runpy.run_path(str(repo/'scripts/host/export-file-check.py'))
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','loaded','png8','png16','exr16','exr32','retained']
    baseline=captures['loaded'];files_evidence=[]
    for stage in stages[2:]:
        assert export_material_state(captures[stage])==export_material_state(baseline),stage
        assert captures[stage]['view']['scene_view_matrix']==baseline['view']['scene_view_matrix'],stage
    for encoding in ['png8','png16','exr16','exr32']:
        evidence=captures[encoding]['exports'][-1];assert evidence['count']=='6 textures selected'
        assert len(evidence['files'])==6 and all(n.startswith('SphereSurface_') for n in evidence['files']) and not any('normal' in n for n in evidence['files'])
        assert 'Export successfully finished' in evidence['console']
        details={}
        for name in evidence['files']:
            path=a.output/encoding/name
            data=readers['png_pixels' if encoding.startswith('png') else 'exr_pixels'](path)
            assert data['width']==data['height']==1024
            if encoding.startswith('png'):assert data['bits']==(8 if encoding=='png8' else 16)
            else:assert set(data['types'].values())=={1 if encoding=='exr16' else 2}
            value=data['sample'](512,512);details[name]={k:v for k,v in data.items() if k!='sample'};details[name]['center']=value
            tolerance={'png8':1/255,'png16':1/65535,'exr16':.0006,'exr32':1e-6}[encoding]
            if '_color.' in name:
                expected=baseline['document']['texture_sets'][1]['base_values']['color'][:3]
                if encoding.startswith('png'):expected=[12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055 for v in expected]
                assert all(abs(c-v)<max(tolerance,1e-4 if encoding.startswith('png') else 1e-6) for c,v in zip(value,expected)),(encoding,name,value,expected)
            if '_roughness.' in name:assert abs(value[0]-.123456)<tolerance,(encoding,name,value)
            if '_bump.' in name:assert abs(value[0]-(0 if encoding.startswith('png') else -.125))<tolerance,(encoding,name,value)
            if '_ambient_occlusion.' in name or '_mixed_ao.' in name:assert abs(value[0]-.5)<tolerance,(encoding,name,value)
        files_evidence.append({'encoding':encoding,'files':details})
    assert captures['retained']['exports'][-1]=={'retained_format':'exr','retained_precision':'32f bits (FLOAT)','retained_count':'6 textures selected'}
    result['texture_export_demo']={'pid':process.pid,'stages':stages,'selected_set_only_six_outputs_each':True,'png8_png16_raw_ihdr_and_samples_verified':True,'exr_half16_float32_actual_chlist_and_scanlines_verified':True,'signed_height_retained_in_exr_png_clamps':True,'normal_maps_not_exported':True,'session_settings_retained':True,'document_and_camera_unchanged':True,'files':files_evidence}
if a.ao:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','baked','added','replace-empty','white-replace','white-multiply','half-multiply','half-replace','undo-mode','redo-mode','ao-view','material','saved','reopened','painted-open','undo-paint-open','removed','undo-remove','cube','returned','ao-context']
    def stats(stage):return captures[stage]['ao']
    def sets(stage):return captures[stage]['document']['texture_sets']
    assert not stats('baked')['enabled'] and stats('baked')['mode']=='Multiply'
    assert sets('added')[1]['ao_channel'] and stats('added')['enabled']
    for stage in ['baked','added','replace-empty','white-multiply','removed']:
        for sample in stats(stage)['samples']:assert abs(sample['value']-sample['baked'])<1e-5,(stage,sample)
    for sample in stats('white-replace')['samples']:assert abs(sample['value']-1)<1e-5,sample
    for stage in ['half-multiply','undo-mode']:
        for sample in stats(stage)['samples']:assert abs(sample['value']-sample['baked']*sample['authored'])<1e-5,(stage,sample)
    for stage in ['half-replace','redo-mode','material','saved','reopened','undo-paint-open','undo-remove','returned','ao-context']:
        for sample in stats(stage)['samples']:assert abs(sample['value']-sample['authored'])<1e-5,(stage,sample)
    assert stats('half-replace')['samples'][0]['value']<.51 and stats('half-replace')['samples'][1]['value']==1
    assert stats('painted-open')['samples'][3]['value']<.26
    assert not stats('removed')['enabled'] and stats('removed')['mode']=='Multiply' and stats('undo-remove')['enabled']
    assert not stats('cube')['enabled'] and stats('returned')['enabled']
    for stage in ['redo-mode','ao-view','material','saved','reopened','undo-paint-open','undo-remove','returned','ao-context']:assert captures[stage]['document']==captures['half-replace']['document'],stage
    assert captures['painted-open']['document']!=captures['reopened']['document']
    errors={}
    for stage,c in captures.items():
        if stage=='initial':continue
        assert c['native']['mesh_objects']==2 and stats(stage)['baked_hash']==stats('baked')['baked_hash']
        assert sets(stage)[0]==sets('baked')[0],stage
        for t in sets(stage):
            for layer in t['layers']:assert layer['paint_pages']==[0]*5,stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert sets('half-replace')[1]['layers'][0]['ao']['tiles'] and sets('removed')[1]['layers'][0]['ao']['tiles']==sets('half-replace')[1]['layers'][0]['ao']['tiles']
    before=Image.open(a.output/'half-multiply-viewport.png').convert('RGB');after=Image.open(a.output/'half-replace-viewport.png').convert('RGB');panes=captures['half-replace']['view']['panes'];differences=[]
    for pane in panes:
        x,y,w,h=pane['rect'];differences.append(sum(ImageStat.Stat(ImageChops.difference(before.crop((x,y,x+w,y+h)),after.crop((x,y,x+w,y+h)))).mean)/3)
    assert differences[0]>.1 and differences[1]<1,(differences)
    result['ao_demo']={'pid':process.pid,'stages':stages,'sixth_optional_ao_channel_uses_separate_authoring_storage':True,'baked_ao_shades_material_before_channel_add':True,'multiply_replace_empty_fallback_and_whole_neutral_exterior':True,'actual_ao_only_white_half_quarter_brush_strokes':True,'baked_map_and_other_set_and_five_legacy_channels_unchanged':True,'packed_ao_role6_checksum_roundtrip_and_further_paint_undo':True,'remove_channel_keeps_data_and_map_undo_restores_mode':True,'unlit_2d_base_color_independent_of_material_ao':True,'layer_ao_context':True,'three_d_mode_difference':differences[0],'two_d_mode_difference':differences[1],'presentation_errors':errors}
if a.height:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','setup','configured','painted','sharp','undo-method','redo-method','height-view','normal-view','material','saved','reopened','smooth-open','undo-open-method','negative','undo-paint-open','removed-height','undo-remove']
    def stats(stage):return captures[stage]['height']
    assert stats('setup')['method']=='Smooth (Sobel)' and stats('setup')['enabled']
    assert captures['configured']['document']==captures['setup']['document'] and stats('configured')['brush']==.2
    assert stats('painted')['samples'][0]['height']>.19 and stats('painted')['samples'][2]['height']==0
    assert stats('painted')['normal_hash']!=stats('setup')['normal_hash']
    assert stats('sharp')['method']=='Sharp' and stats('sharp')['normal_hash']!=stats('painted')['normal_hash']
    assert stats('sharp')['bump_hash']==stats('painted')['bump_hash']
    assert captures['undo-method']['document']==captures['painted']['document'] and stats('undo-method')['normal_hash']==stats('painted')['normal_hash']
    for stage in ['redo-method','height-view','normal-view','material','saved','reopened','undo-open-method','undo-paint-open','undo-remove']:
        assert captures[stage]['document']==captures['sharp']['document'],stage
        assert stats(stage)['normal_hash']==stats('sharp')['normal_hash'] and stats(stage)['bump_hash']==stats('sharp')['bump_hash'],stage
    assert stats('smooth-open')['normal_hash']==stats('painted')['normal_hash']
    assert stats('negative')['samples'][1]['height']<-.19
    assert not stats('removed-height')['enabled'] and stats('removed-height')['method']=='Smooth (Sobel)' and stats('removed-height')['normal_hash']==stats('setup')['normal_hash']
    errors={}
    for stage,c in captures.items():
        if stage=='initial':continue
        assert c['native']['mesh_objects']==1 and c['native']['uv_triangles']==2
        assert c['native']['bounds_minimum']==captures['setup']['native']['bounds_minimum'] and c['native']['bounds_maximum']==captures['setup']['native']['bounds_maximum']
        assert stats(stage)['baked_hash']==stats('setup')['baked_hash']
        for layer in c['document']['texture_sets'][0]['layers']:assert all(layer['paint_pages'][ch]==0 for ch in [0,1,2,3]),stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    pane=captures['painted']['view']['panes'][0]['rect'];x,y,w,h=pane
    first=Image.open(a.output/'painted-viewport.png').convert('RGB').crop((x,y,x+w,y+h));second=Image.open(a.output/'configured-viewport.png').convert('RGB').crop((x,y,x+w,y+h));delta=ImageChops.difference(first,second);difference=sum(ImageStat.Stat(delta).mean)/3;changed=sum(max(pixel)>2 for pixel in delta.getdata());peak=max(channel[1] for channel in delta.getextrema());assert changed>100 and peak>8,(changed,peak)
    result['height_demo']={'pid':process.pid,'stages':stages,'height_only_point2_and_negative_paint_affect_shading_without_geometry_changes':True,'sharp_and_sobel_change_complete_normal_pixels_but_preserve_height':True,'raw_height_and_complete_normal_inspection':True,'method_undo_redo_and_channel_dependency':True,'schema13_packed_roundtrip_and_continued_negative_paint':True,'baked_normal_and_other_authoring_channels_unchanged':True,'material_view_mean_difference':difference,'material_changed_pixels':changed,'material_peak_rgb_difference':peak,'presentation_errors':errors}
if a.normal_mix:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','empty-combine','empty-replace','stroke','undo-stroke','fill-replace','fill-combine','fill-replace-again','removed','undo-remove','combine','channel','material','saved','reopened','replace-open','undo-mode','edited-open','undo-edit','hidden','shown']
    def stats(stage):return captures[stage]['normal_mix']
    def direction(stage,index=0):
        rgba=stats(stage)['samples'][index]['rgba'];assert rgba[3]==1,(stage,rgba)
        return [value*2-1 for value in rgba[:3]]
    def near(a,b):return max(abs(x-y) for x,y in zip(a,b))<3e-5
    baked=stats('empty-combine')['baked'];assert abs(baked[0])>.05 and abs(baked[1])<1e-4
    for stage in ['empty-combine','empty-replace','undo-stroke','removed','hidden']:
        for i in [0,1]:assert near(direction(stage,i),baked),(stage,direction(stage,i),baked)
    assert stats('stroke')['samples'][0]['coverage']>0 and stats('stroke')['samples'][1]['coverage']==0
    for i in [0,1]:assert near(direction('stroke',i),stats('stroke')['samples'][i]['authored'])
    assert near(direction('stroke',1),[0,0,1]) and not near(direction('stroke',1),baked)
    for stage in ['fill-replace','fill-replace-again','undo-remove','replace-open']:
        assert stats(stage)['mode']=='Replace'
        for i in [0,1]:assert near(direction(stage,i),stats(stage)['samples'][i]['authored']),stage
    detail=stats('fill-combine')['samples'][0]['authored']
    def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
    axis=[-baked[1],baked[0],0];first=cross(axis,detail);second=cross(axis,first);expected=[detail[i]+first[i]+second[i]/(1+baked[2]) for i in range(3)]
    for stage in ['fill-combine','combine','channel','material','saved','reopened','undo-mode','undo-edit','shown']:
        for i in [0,1]:assert near(direction(stage,i),expected),(stage,direction(stage,i),expected)
    assert not stats('removed')['enabled'] and stats('removed')['mode']=='Combine' and stats('undo-remove')['enabled']
    assert captures['undo-stroke']['document']==captures['empty-replace']['document']
    assert captures['undo-remove']['document']==captures['fill-replace-again']['document']
    for stage in ['channel','material','saved','reopened','undo-mode','undo-edit','shown']:assert captures[stage]['document']==captures['combine']['document'],stage
    assert captures['edited-open']['document']!=captures['reopened']['document']
    errors={}
    for stage,c in captures.items():
        if stage=='initial':continue
        assert stats(stage)['baked_hash']==stats('empty-combine')['baked_hash']
        assert c['native']['mesh_objects']==1 and c['document']['texture_sets'][0]['mesh_maps'][0]==captures['empty-combine']['document']['texture_sets'][0]['mesh_maps'][0]
        if stage not in ['stroke']:
            for layer in c['document']['texture_sets'][0]['layers']:assert layer['paint_pages']==[0]*5,stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    pane=captures['fill-combine']['view']['panes'][0]['rect'];x,y,w,h=pane
    first=Image.open(a.output/'fill-combine-viewport.png').convert('RGB').crop((x,y,x+w,y+h));second=Image.open(a.output/'fill-replace-viewport.png').convert('RGB').crop((x,y,x+w,y+h));difference=sum(ImageStat.Stat(ImageChops.difference(first,second)).mean)/3;assert difference>.1,difference
    uv_pane=captures['material']['view']['panes'][1]['rect'];ux,uy,uw,uh=uv_pane
    flat=Image.open(a.output/'material-viewport.png').convert('RGB').crop((ux+uw//4,uy+uh*3//10,ux+uw*3//4,uy+uh*7//10));flat_deviation=max(ImageStat.Stat(flat).stddev);assert flat_deviation<1.2,flat_deviation
    result['normal_mix_demo']={'pid':process.pid,'stages':stages,'baked_normal_affects_live_material':True,'distinct_nonflat_authored_baked_pair_matches_analytic_rotation':True,'replace_empty_fallback_and_whole_channel_outside_first_stroke':True,'removed_channel_keeps_baked_map_and_undo_restores_mode':True,'normal_xyz_properties_edit_actual_fill_and_brush':True,'schema12_exact_save_open_and_further_edits':True,'baked_data_and_authoring_pages_preserved_by_mode_changes':True,'material_view_mean_difference':difference,'unlit_uniform_2d_material_stddev':flat_deviation,'presentation_errors':errors}
if a.utility_maps:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','clear-cancel','legacy','legacy-ao','corrected','normal','rejected','saved','reopened','ao-open','painted','undo-paint']
    evidence={e['label']:e for e in captures['undo-paint']['utility']['evidence']}
    assert evidence['cleared']['count']==0 and len(evidence['legacy-list']['files'])==len(evidence['base-list']['files'])==3
    assert captures['clear-cancel']['document']==captures['initial']['document']
    def maps(stage):return captures[stage]['utility']['maps']
    assert maps('legacy')[0]==[None]*7 and maps('legacy')[1][3]['name']=='SphereSurface_ambient_occlusion'
    assert maps('corrected')[0][0]['name']=='CubeSurface_normal_base' and maps('corrected')[1][3]==maps('legacy')[1][3]
    for set_index in [0,1]:
        for role in range(7):
            if (set_index,role) not in [(0,0),(1,3)]:assert maps('corrected')[set_index][role] is None
    assert evidence['rejected']['document_preserved'] and evidence['rejected']['native_preserved'] and 'decode' in evidence['rejected']['error'].lower()
    for stage in ['normal','rejected','saved','reopened','ao-open','undo-paint']:assert maps(stage)==maps('corrected'),stage
    assert not list(utility_source.glob('*.png'))
    for set_index,role,filename in [(0,0,'CubeSurface_normal_base.png'),(1,3,'SphereSurface_ambient_occlusion.png')]:
        im=Image.open(a.utility_maps/filename).convert('RGB');width,height=im.size;expected=[]
        points=[(0,height-1),(width//3,height-1-height*2//5),(width-1,0)]
        # Pillow truncates 16-bit RGB PNG to RGB8; independently unfilter those source bytes.
        raw=(a.utility_maps/filename).read_bytes();depth,color=raw[24],raw[25]
        if depth==8:expected=[[v/255 for v in im.getpixel(point)]+[1] for point in points]
        else:
            import zlib
            assert depth==16 and raw[28]==0
            channels={0:1,2:3,4:2,6:4}[color];bpp=channels*2;stride=width*bpp;compressed=bytearray();offset=8
            while offset<len(raw):
                size=int.from_bytes(raw[offset:offset+4],'big');kind=raw[offset+4:offset+8]
                if kind==b'IDAT':compressed.extend(raw[offset+8:offset+8+size])
                offset+=12+size
            scan=zlib.decompress(compressed);previous=bytearray(stride);samples={}
            for y in range(height):
                start=y*(stride+1);filter_type=scan[start];row=bytearray(scan[start+1:start+1+stride])
                for x in range(stride):
                    left=row[x-bpp] if x>=bpp else 0;up=previous[x];corner=previous[x-bpp] if x>=bpp else 0
                    if filter_type==1:predictor=left
                    elif filter_type==2:predictor=up
                    elif filter_type==3:predictor=(left+up)//2
                    elif filter_type==4:
                        estimate=left+up-corner;distances=[abs(estimate-left),abs(estimate-up),abs(estimate-corner)];predictor=[left,up,corner][distances.index(min(distances))]
                    else:assert filter_type==0;predictor=0
                    row[x]=(row[x]+predictor)&255
                for px,py in points:
                    if py==y:samples[(px,py)]=[int.from_bytes(row[px*bpp+(0 if channels<3 else c)*2:px*bpp+(0 if channels<3 else c)*2+2],'big')/65535 for c in range(3)]+[1]
                previous=row
            expected=[samples[point] for point in points]
        resource=maps('corrected')[set_index][role];assert (resource['resolution'],resource['height'])==(width,height)
        for actual,reference in zip(resource['samples'],expected):assert max(abs(a-b) for a,b in zip(actual,reference))<1e-6,(filename,actual,reference)
    errors={}
    for stage,c in captures.items():
        assert c['workspace']['texture_sets']==['CubeSurface','SphereSurface'] and c['native']['mesh_objects']==2
        if stage not in ['initial','clear-cancel','painted']:
            for t in c['document']['texture_sets']:assert len(t['layers'])==1 and t['layers'][0]['paint_pages']==[0]*5,stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    assert captures['painted']['document']!=captures['reopened']['document'] and captures['undo-paint']['document']==captures['reopened']['document']
    result['utility_map_demo']={'pid':process.pid,'stages':stages,'actual_add_file_choosers_clear_and_cancel':True,'normal_base_and_ambient_occlusion_assign_only_named_sets':True,'legacy_normal_and_wrong_set_do_not_create_layers_or_sets':True,'raw_native_data_decode_matches_png_samples_without_gamma':True,'corrupt_map_rejected_before_live_project_replacement':True,'source_copies_deleted_before_exact_packed_float_open':True,'paint_and_undo_after_open':True,'presentation_errors':errors}
if a.bake_match:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','settings','empty-case','matched','always','undo-always','redo-always','case-source','restored','saved','reopened','inspected','retained','painted','undo-paint']
    evidence={e['label']:e for e in captures['undo-paint']['matching']['evidence']}
    e=evidence['defaults'];assert (e['match'],e['low'],e['high'],e['ignore'])==('Always','_low','_high','_ignorebf') and e['low_enabled'] and e['high_enabled'] and e['ignore_enabled']
    e=evidence['uppercase'];assert (e['match'],e['low'],e['high'])==('By Mesh Name','_LOW','_HIGH') and e['count']=='2 textures selected for baking'
    assert captures['settings']['document']==captures['initial']['document']
    def role(stage,index):return captures[stage]['matching']['maps'][0][index]
    assert role('empty-case',0)['covered']==role('empty-case',1)['covered']==0
    def dominant(pixel,channel):return pixel[3]==1 and pixel[channel]>.5 and pixel[channel]>max(pixel[i] for i in range(3) if i!=channel)
    ids=role('matched',1)['centers'];assert dominant(ids[0],0) and dominant(ids[1],1),ids
    assert all(dominant(pixel,2) for pixel in role('always',1)['centers'])
    normals=role('matched',0)['centers'];assert max(abs(normals[0][c]-normals[1][c]) for c in range(3))>.005
    assert captures['undo-always']['document']==captures['matched']['document'] and captures['redo-always']['document']==captures['always']['document']
    partial=role('case-source',1)['centers'];assert dominant(partial[0],0) and partial[1]==[0,0,0,0]
    for stage in ['restored','saved','reopened','inspected','retained','undo-paint']:assert captures[stage]['document']==captures['matched']['document'],stage
    e=evidence['retained'];assert (e['match'],e['low'],e['high'],e['ignore'])==('By Mesh Name','_low','_high','_ignorebf')
    assert captures['painted']['document']!=captures['matched']['document']
    errors={}
    for stage,c in captures.items():
        assert c['native']['mesh_objects']==2 and c['native']['uv_triangles']==4 and len(c['document']['texture_sets'])==1
        assert [m['name'] for m in c['native']['materials']]==['SharedSurface']
        if stage!='painted':assert c['document']['texture_sets'][0]['layers']==captures['initial']['document']['texture_sets'][0]['layers'],stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['bake_match_demo']={'pid':process.pid,'stages':stages,'editable_case_preserving_suffix_controls':True,'two_low_objects_in_one_texture_set_filter_separately':True,'overlapping_unrelated_high_mesh_excluded_before_rays':True,'high_geometry_case_mismatch_leaves_only_that_low_island_empty':True,'normal_and_id_centers_prove_pair_isolation':True,'undo_redo_exact_map_roundtrip_and_paint_after_open':True,'statistics':{stage:captures[stage]['matching']['maps'] for stage in ['empty-case','matched','always','case-source']},'presentation_errors':errors}
if a.cage:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','settings','miss','distance','relative','full','undo','redo','cage','rejected','settings-retained','saved','reopened','inspected-open','painted','undo-paint','retained']
    evidence={e['label']:e for e in captures['retained']['cage']['evidence']}
    defaults=evidence['defaults'];assert not defaults['cage'] and defaults['file']=='' and defaults['frontal']==defaults['rear']==.01 and defaults['relative'] and defaults['average'] and defaults['backface']
    for name in ['enabled-empty','selected-file']:
        e=evidence[name];assert e['cage'] and not any(e[k] for k in ['frontal_enabled','rear_enabled','relative_enabled','average_enabled']) and e['backface_enabled']
    assert not evidence['enabled-empty']['bake_enabled'] and evidence['selected-file']['bake_enabled']
    e=evidence['distance-settings'];assert not e['cage'] and e['file'].endswith('/cage_valid.obj') and e['frontal']==.05 and e['rear']==.02 and not e['relative'] and e['frontal_enabled'] and e['high_count']==1
    assert 'vertex count' in evidence['mismatch-error']['error'].lower()
    assert evidence['retained']['file'].endswith('/cage_valid.obj') and evidence['retained']['cage'] and not evidence['retained']['frontal_enabled']
    assert captures['settings']['document']==captures['initial']['document']
    def plane(stage):return captures[stage]['cage']['maps'][0][0]
    assert plane('miss')['covered']==0
    assert 0<plane('distance')['covered']<plane('relative')['covered']<plane('full')['covered']
    assert plane('full')['covered']==plane('cage')['covered']==128*128
    for stage in ['full','cage']:
        for bound in ['low','high']:
            value=plane(stage)[bound];assert abs(abs(value[0]-.5)-.05/math.sqrt(1.01))<3e-5 and abs(value[1]-.5)<3e-5 and abs(value[2]-(.5+.5/math.sqrt(1.01)))<3e-5,(stage,value)
    assert captures['undo']['document']==captures['relative']['document'] and captures['redo']['document']==captures['full']['document']
    for stage in ['rejected','settings-retained','saved','reopened','inspected-open','undo-paint','retained']:assert captures[stage]['document']==captures['cage']['document'],stage
    assert captures['painted']['document']!=captures['cage']['document']
    errors={}
    for stage,c in captures.items():
        assert c['native']['mesh_objects']==1 and c['native']['uv_triangles']==2
        assert c['native']['bounds_minimum']==captures['initial']['native']['bounds_minimum'] and c['native']['bounds_maximum']==captures['initial']['native']['bounds_maximum']
        assert [m['name'] for m in c['native']['materials']]==['LowSurface']
        if stage!='painted':assert c['document']['texture_sets'][0]['layers']==captures['initial']['document']['texture_sets'][0]['layers'],stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['cage_demo']={'pid':process.pid,'stages':stages,'actual_obj_file_choosers_and_cage_dependency_states':True,'native_high_source_without_uvs_staged_without_replacing_live_scene':True,'frontal_distance_and_bounds_normalization_change_hit_coverage':True,'custom_cage_transfers_tilted_high_normal_with_native_tangent_conversion':True,'mismatched_cage_preserves_document_and_history':True,'undo_and_exact_packed_map_roundtrip':True,'editable_after_open':True,'coverage':{stage:plane(stage) for stage in ['miss','distance','relative','full','cage']},'presentation_errors':errors}
if a.bake_quality:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','settings','rectangular','inspected','undo','redo','saved','reopened','no-aa','aa2','aa4','aa8','dilation','diffusion','undo-diffusion','retained']
    evidence={e['label']:e for e in captures['retained']['quality']['evidence']}
    defaults=evidence['defaults'];assert (defaults['width'],defaults['height'],defaults['locked'],defaults['height_enabled'],defaults['dilation'],defaults['diffusion'],defaults['aa'])==(512,512,True,False,32,False,1)
    assert evidence['locked1024']['height']==1024 and not evidence['locked1024']['height_enabled']
    configured=evidence['configured'];assert (configured['width'],configured['height'],configured['locked'],configured['dilation'],configured['diffusion'],configured['aa'])==(1024,512,False,8,True,2)
    assert evidence['maximum']['width']==8192 and evidence['maximum']['height']==512
    assert configured['count']=='1 texture selected for baking'
    assert captures['settings']['document']==captures['initial']['document']==captures['undo']['document']
    def plane(stage):
        maps=captures[stage]['quality']['maps'];assert len(maps)==1 and len(maps[0])==1 and maps[0][0]['tile']==1001
        return maps[0][0]
    rect=plane('rectangular');assert (rect['width'],rect['height'])==(1024,512) and rect['zero']==0
    for stage in ['inspected','redo','saved','reopened']:assert plane(stage)==rect,stage
    assert captures['saved']['document']==captures['reopened']['document']
    noaa=plane('no-aa');assert (noaa['width'],noaa['height'])==(128,64) and noaa['fractional']==0 and noaa['zero']>1000
    for stage in ['aa2','aa4','aa8']:assert plane(stage)['fractional']>0 and plane(stage)['zero']>1000,stage
    assert len({plane(stage)['hash'] for stage in ['no-aa','aa2','aa4','aa8']})==4
    assert plane('dilation')['zero']<plane('aa8')['zero'] and plane('diffusion')['zero']==0
    assert plane('undo-diffusion')==plane('retained')==plane('dilation')
    assert evidence['retained']['diffusion'] and evidence['retained']['aa']==8 and evidence['retained']['height']==64
    errors={}
    for stage,c in captures.items():
        assert c['document']['texture_sets'][0]['layers']==captures['initial']['document']['texture_sets'][0]['layers'],stage
        assert c['document']['texture_sets'][0]['tiles']==[1001,1002]
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['bake_quality_demo']={'pid':process.pid,'stages':stages,'locked_and_independent_dimensions':True,'rectangular_1024x512_float_map_exact_partial_udim_roundtrip':True,'native_2x2_4x4_8x8_supersampling_changes_fractional_edge_coverage':True,'native_dilation_reduces_empty_pixels':True,'push_pull_diffusion_fills_exterior_without_blurring_islands':True,'session_settings_independent_of_undo':True,'unselected_tile_stays_unassigned':True,'statistics':{stage:plane(stage) for stage in ['rectangular','no-aa','aa2','aa4','aa8','dilation','diffusion']},'presentation_errors':errors}
if a.selective_bake:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','settings-saved','retained','partial','inspected-partial','all-baked','rebaked','undo','redo','retained-again','saved','reopened','inspected-open','painted','undo-paint']
    evidence={e['label']:e for e in captures['undo-paint']['selection_evidence']}
    assert evidence['all']['count']=='14 textures selected for baking'
    assert evidence['one-tile']['count']=='7 textures selected for baking'
    assert evidence['normal']['count']=='1 texture selected for baking'
    assert evidence['none']['count']=='No textures selected for baking' and not evidence['none']['bake_enabled']
    assert evidence['retained']['count']=='1 texture selected for baking' and evidence['retained']['maps']==[True]+[False]*6
    assert evidence['complete2']['count']==evidence['complete4']['count']==1 and evidence['complete3']['count']==14
    assert len(evidence['complete2']['tasks'])==1 and '1001' in evidence['complete2']['tasks'][0]
    for stage in ['settings-saved','retained']:assert captures[stage]['document']==captures['initial']['document'],stage
    def sets(stage):return captures[stage]['document']['texture_sets']
    partial=sets('partial');assert partial[0]['mesh_maps'][0]['tiles'][0]['number']==1001
    assert all(m is None for m in partial[0]['mesh_maps'][1:])
    if a.selective_udim:
        assert partial[0]['tiles']==[1001,1002] and len(partial[0]['mesh_maps'][0]['tiles'])==1
    else:assert partial[1]['mesh_maps']==sets('initial')[1]['mesh_maps']
    full=sets('all-baked');rebaked=sets('rebaked')
    assert rebaked[0]['mesh_maps'][0]['tiles'][0]['resolution']==256
    assert rebaked[0]['mesh_maps'][1:]==full[0]['mesh_maps'][1:]
    if a.selective_udim:assert rebaked[0]['mesh_maps'][0]['tiles'][1]==full[0]['mesh_maps'][0]['tiles'][1]
    else:assert rebaked[1]==full[1]
    assert sets('undo')==full
    for stage in ['redo','retained-again','saved','reopened','inspected-open','undo-paint']:assert sets(stage)==rebaked,stage
    assert sets('painted')!=rebaked
    errors={}
    for stage,c in captures.items():
        if stage!='painted':
            for i,t in enumerate(c['document']['texture_sets']):assert t['layers']==sets('initial')[i]['layers'],stage
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['selective_bake_demo']={'pid':process.pid,'stages':stages,'udim':a.selective_udim,'set_tile_and_baker_filter_count_and_execution':True,'zero_selection_disabled':True,'session_selection_retained':True,'unselected_map_set_and_tile_checksums_unchanged':True,'mixed_resolution_tiles_roundtrip_and_undo':True,'editable_after_open':True,'presentation_errors':errors}
if a.mesh_maps:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['initial','cancelled','baked','normal','world','id','ao','curvature','position','thickness','backwards','material','undo','redo','saved','reopened','inspected-open','removed','undo-remove','painted','undo-paint']
    first=captures['initial'];assert captures['cancelled']['document']==first['document']
    defaults=captures['cancelled']['bake_defaults'];assert defaults=={'resolution':512,'dilation':32,'rays':64,'source_checked':False,'selected_count':'14 textures selected for baking'}
    maps=captures['baked']['mesh_maps'];assert len(maps)==2 and all(len(m)==7 and all(role and role[0]['resolution']==512 and role[0]['covered']>1000 for role in m) for m in maps)
    assert maps[1][0][0]['low']==maps[1][0][0]['high']==[.5,.5,1]
    assert maps[1][1][0]['high'][0]-maps[1][1][0]['low'][0]>.5 and maps[1][5][0]['high'][0]-maps[1][5][0]['low'][0]>.1
    assert maps[0][3][0]['low'][0]<.99 or maps[1][3][0]['low'][0]<.99
    for stage,role in [('normal',0),('world',1),('id',2),('ao',3),('curvature',4),('position',5),('thickness',6),('backwards',5),('inspected-open',5)]:assert captures[stage]['mesh_map_mode']==role,(stage,captures[stage]['mesh_map_mode'])
    assert captures['undo']['document']==first['document']
    for stage in ['redo','saved','reopened','inspected-open','undo-remove','undo-paint']:assert captures[stage]['document']==captures['baked']['document'],stage
    assert captures['removed']['document']!=captures['reopened']['document'] and captures['painted']['document']!=captures['reopened']['document']
    errors={}
    for stage,c in captures.items():
        if stage!='painted':
            for set_index in [0,1]:assert c['document']['texture_sets'][set_index]['layers']==first['document']['texture_sets'][set_index]['layers']
        if stage not in ['initial','cancelled','undo','removed']:assert c['mesh_maps']==maps,stage
        if stages.index(stage)<15:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['mesh_maps_demo']={'pid':process.pid,'stages':stages,'fourteen_low_poly_maps_assigned_atomically':True,'native_bake_atlas_and_bvh_ao_thickness':True,'seven_flat_mesh_map_inspection_modes':True,'cancel_and_whole_bake_undo_redo_preserve_layers':True,'packed_float_maps_save_open_exact':True,'map_removal_and_continued_painting_after_open':True,'presentation_errors':errors}
if a.smart_material:
    demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
    captures={c['stage']:c for c in demo['captures']};stages=list(captures)
    assert stages==['missing','loaded','applied','undo','redo','child','edited','undo-child','map-view','mask-view','material','selector','undo-selector','saved','reopened','edited-open','undo-open','dragged','undo-drag']
    def stats(stage):return captures[stage]['smart_material']
    assert not stats('missing')['ready'] and stats('loaded')['ready']
    assert stats('applied')['folders']==1 and stats('selector')['folders']==stats('dragged')['folders']==2
    for before,after in [('loaded','undo'),('applied','redo'),('child','undo-child'),('material','undo-selector'),('saved','reopened'),('saved','undo-open'),('saved','undo-drag')]:assert captures[before]['document']==captures[after]['document'],(before,after)
    assert captures['edited']['document']!=captures['child']['document'] and captures['edited-open']['document']!=captures['reopened']['document']
    assert stats('map-view')['mesh_map_mode']>=0 and stats('mask-view')['mesh_map_mode']==-1 and stats('mask-view')['inspection']==5
    first=captures['loaded'];errors={}
    for stage,c in captures.items():
        if stage=='missing':continue
        assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0]
        for index in [0,1]:assert c['document']['texture_sets'][index]['mesh_maps']==first['document']['texture_sets'][index]['mesh_maps']
        if stage not in ['reopened','edited-open','undo-open','dragged','undo-drag']:assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix']
        image=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');window=Image.open(a.output/f'{stage}-window.png').convert('RGB');x,y,w,h=c['viewport_rect']
        errors[stage]=sum(ImageStat.Stat(ImageChops.difference(image,window.crop((x,y,x+w,y+h)))).mean)/(3*255);assert errors[stage]<.02
    result['smart_material_demo']={'pid':process.pid,'stages':stages,'three_layer_map_informed_folder_from_shelf_and_selector':True,'actual_shelf_asset_drop_creates_independent_folder':True,'atomic_folder_undo_redo':True,'editable_child_parameters_and_masks_after_save_open':True,'mesh_map_to_mask_isolation_regression':True,'source_mesh_maps_and_other_set_unchanged':True,'presentation_errors':errors}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'pid': process.pid, 'texture_sets':result['workspace']['texture_sets'],
                  'mesh_objects':result['native']['mesh_objects'], 'triangles': result['native']['uv_triangles'],
                  'invalid_import_preserved':True, 'profile_verified':True,
                  'presentation_error':error, 'clean_exit':True, 'children':list(children)},indent=2))
