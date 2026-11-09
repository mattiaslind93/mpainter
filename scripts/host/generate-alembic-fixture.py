# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with stock Blender --background --python SCRIPT -- OUTPUT_DIR."""
import bpy,sys
from pathlib import Path
out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,radius=1)
obj=bpy.context.object;obj.name='AlembicSurface'
# Include an animated transform: the app must freeze the first frame and drop caches.
obj.location=(.25,0,0);obj.keyframe_insert(data_path='location',frame=1)
obj.location=(.75,0,0);obj.keyframe_insert(data_path='location',frame=2)
for name,scale in [('surface.abc',1),('replacement.ABC',1.1)]:
 obj.scale=(scale,scale,scale)
 bpy.ops.wm.alembic_export(filepath=str(out/name),start=1,end=2,selected=True,uvs=True,face_sets=True,as_background_job=False)
print('BTS_ALEMBIC_FIXTURES',out)
