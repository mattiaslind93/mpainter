# SPDX-License-Identifier: GPL-3.0-or-later
"""Real OBJ / Alembic / USDC exports, shared two-UDIM mesh and animated sources."""
import bpy,sys,math
from pathlib import Path
out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=2
material=bpy.data.materials.new('BakeSurface');material.use_nodes=True

def objects(kind):
 result=[]
 for tile in [0,1]:
  n=8 if kind=='high'else 1;verts=[];faces=[]
  for y in range(n+1):
   for x in range(n+1):
    u=x/n;v=y/n;z=(.07+.12*math.sin(math.pi*u)*math.sin(math.pi*v)*(tile+1))if kind=='high'else(.4 if kind=='cage'else 0)
    verts.append((u*2-1,v*2-1,z))
  for y in range(n):
   for x in range(n):
    a=y*(n+1)+x;faces.append((a,a+1,a+n+2,a+n+1))
  mesh=bpy.data.meshes.new(f'Part{tile}_{kind}');mesh.from_pydata(verts,[],faces);mesh.update();uv=mesh.uv_layers.new()
  for poly in mesh.polygons:
   for i in poly.loop_indices:
    co=mesh.vertices[mesh.loops[i].vertex_index].co;uv.data[i].uv=(tile+.1+.8*(co.x+1)/2,.1+.8*(co.y+1)/2)
  ob=bpy.data.objects.new(mesh.name,mesh);bpy.context.collection.objects.link(ob);ob.location=(tile*3,.25,0);ob.data.materials.append(material);result.append(ob)
 return result

def export(kind):
 bpy.ops.object.select_all(action='DESELECT');obs=objects(kind)
 for ob in obs:ob.select_set(True)
 bpy.context.view_layer.objects.active=obs[0];bpy.context.scene.frame_set(1)
 bpy.ops.wm.obj_export(filepath=str(out/f'{kind}.obj'),export_selected_objects=True,forward_axis='NEGATIVE_Z',up_axis='Y')
 bpy.ops.wm.alembic_export(filepath=str(out/f'{kind}.abc'),start=1,end=1,selected=True,uvs=True,face_sets=True,as_background_job=False)
 bpy.ops.wm.usd_export(filepath=str(out/f'{kind}.usdc'),selected_objects_only=True,export_animation=False,export_uvmaps=True,export_materials=False)
 if kind=='high':
  bpy.ops.wm.alembic_export(filepath=str(out/'high-cm.abc'),start=1,end=1,selected=True,uvs=True,face_sets=True,global_scale=100,as_background_job=False)
  for ob in obs:
   ob.keyframe_insert(data_path='location',frame=1);ob.location.z+=.2;ob.keyframe_insert(data_path='location',frame=2)
  bpy.ops.wm.alembic_export(filepath=str(out/'animated.abc'),start=1,end=2,selected=True,uvs=True,face_sets=True,as_background_job=False)
  bpy.ops.wm.usd_export(filepath=str(out/'animated.usdc'),selected_objects_only=True,export_animation=True,export_uvmaps=True,export_materials=False)
  bpy.context.scene.frame_set(1)
 for ob in obs:bpy.data.objects.remove(ob,do_unlink=True)
for kind in ['low','high','cage']:export(kind)
# Identical geometry in a USD layer authored in centimetres.
from pxr import Usd,UsdGeom,Gf
stage=Usd.Stage.Open(str(out/'high.usdc'));UsdGeom.SetStageMetersPerUnit(stage,.01)
for prim in stage.Traverse():
 if prim.IsA(UsdGeom.Mesh):
  a=UsdGeom.Mesh(prim).GetPointsAttr();a.Set([Gf.Vec3f(p*100) for p in a.Get()])
 for attr in prim.GetAttributes():
  if attr.GetName().startswith('xformOp:transform'):
   v=attr.Get()
   if v is not None:v.SetRow(3,Gf.Vec4d(v[3][0]*100,v[3][1]*100,v[3][2]*100,v[3][3]));attr.Set(v)
  if attr.GetName().startswith('xformOp:translate'):
   v=attr.Get()
   if v is not None:attr.Set(v*100)
stage.GetRootLayer().Export(str(out/'high-cm.usdc'))
print('BTS_BAKE_SOURCE_FIXTURES',out)
