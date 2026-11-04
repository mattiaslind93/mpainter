"""Run in background Blender, one material/process. Original CC0 starter shaders."""
import bpy, json, math, sys
from pathlib import Path
from mathutils import Vector
mapping, textures, output, mesh, slug = sys.argv[sys.argv.index('--')+1:]
row=next(r for r in json.loads(Path(mapping).read_text()) if r['slug']==slug)
out=Path(output);out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
m=bpy.data.materials.new(row['name']);m.use_nodes=True;m.use_fake_user=True
n=m.node_tree.nodes;l=m.node_tree.links;n.clear()
def node(kind,name=None):
 x=n.new('ShaderNode'+kind)
 if name:x.label=name;x.name=name
 return x
def link(a,b):l.new(a,b)
def mathnode(op,a,b=None):
 x=node('Math');x.operation=op
 for i,v in enumerate([a,b]):
  if v is None:continue
  if isinstance(v,(float,int)):x.inputs[i].default_value=v
  else:link(v,x.inputs[i])
 return x.outputs[0]
def ramp(value,low,high):
 x=node('ValToRGB');x.color_ramp.elements[0].color=(*low,1);x.color_ramp.elements[1].color=(*high,1);link(value,x.inputs[0]);return x.outputs[0]
shader=node('BsdfPrincipled','Surface');surface=node('OutputMaterial');link(shader.outputs[0],surface.inputs['Surface'])
coord=node('TexCoord');mapping_node=node('Mapping','Surface Mapping');link(coord.outputs['UV'],mapping_node.inputs['Vector']);uv=mapping_node.outputs['Vector']
if row['maps']:
 maps={}
 for channel,r in row['maps'].items():
  tex=node('TexImage',channel);tex.image=bpy.data.images.load(str(Path(textures)/r['file']));tex.image.colorspace_settings.name='sRGB' if channel=='Diffuse' else 'Non-Color';link(uv,tex.inputs['Vector']);maps[channel]=tex.outputs['Color']
 color=maps['Diffuse']
 if 'AO' in maps:
  mix=node('MixRGB','Diffuse × AO');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;link(color,mix.inputs[1]);link(maps['AO'],mix.inputs[2]);color=mix.outputs[0]
 link(color,shader.inputs['Base Color']);link(maps['Rough'],shader.inputs['Roughness'])
 if 'Metal' in maps:link(maps['Metal'],shader.inputs['Metallic'])
 elif row['name']=='Galvanized Steel':shader.inputs['Metallic'].default_value=1.0
 normal=node('NormalMap','OpenGL Normal');link(maps['nor_gl'],normal.inputs['Color']);link(normal.outputs[0],shader.inputs['Normal'])
 if 'Displacement' in maps:
  displacement=node('Displacement','Surface Displacement');displacement.inputs['Scale'].default_value=.015;link(maps['Displacement'],displacement.inputs['Height']);link(displacement.outputs[0],surface.inputs['Displacement'])
else:
 name=row['name']
 colors={'Gold':(.83,.55,.16),'Copper':(.8,.34,.16),'Rubber':(.018,.022,.026),'Silicone':(.42,.14,.09),'Powder Coat':(.035,.12,.22),'Grainy Plastic':(.14,.23,.32),'Striped Plastic':(.65,.12,.035),'Diamond Plate Plastic':(.045,.055,.065),'Carbon Fibre':(.028,.032,.038),'Reptile Scales':(.12,.22,.055),'Rough Iron':(.28,.29,.31)}
 metal=name in ['Gold','Copper','Aluminium Ribbed','Brushed Iron','Hammered Metal','Polished Iron','Raw Iron','Rough Steel','Rough Iron']
 base=colors.get(name,(.55,.57,.6));shader.inputs['Base Color'].default_value=(*base,1);shader.inputs['Metallic'].default_value=int(metal)
 rough={'Gold':.2,'Copper':.25,'Polished Iron':.15,'Rubber':.76,'Silicone':.4,'Powder Coat':.65,'Rough Steel':.68,'Raw Iron':.52,'Rough Iron':.76}.get(name,.35)
 shader.inputs['Roughness'].default_value=rough
 noise=node('TexNoise','Microstructure');link(uv,noise.inputs['Vector']);noise.inputs['Scale'].default_value=140;noise.inputs['Detail'].default_value=2
 height=noise.outputs['Fac'];strength=.14;distance=.008
 if name in ['Aluminium Ribbed','Striped Plastic','Brushed Iron']:
  wave=node('TexWave','Linear grain');wave.bands_direction='X';wave.inputs['Scale'].default_value=100 if name=='Brushed Iron' else 16;link(uv,wave.inputs['Vector']);height=wave.outputs['Fac'];strength=.23;distance=.001 if name=='Brushed Iron' else .018
 elif name=='Hammered Metal':
  vor=node('TexVoronoi','Hammer dents');vor.inputs['Scale'].default_value=38;link(uv,vor.inputs['Vector']);height=vor.outputs['Distance'];strength=.4;distance=.025
 elif name=='Carbon Fibre':
  check=node('TexChecker','Over-under weave');check.inputs['Scale'].default_value=65;link(uv,check.inputs['Vector'])
  waves=[]
  for axis in ['X','Y']:
   wave=node('TexWave');wave.bands_direction=axis;wave.inputs['Scale'].default_value=260;link(uv,wave.inputs['Vector']);waves.append(wave.outputs['Fac'])
  mix=node('MixRGB','Alternating fibre direction');link(check.outputs['Fac'],mix.inputs[0]);link(waves[0],mix.inputs[1]);link(waves[1],mix.inputs[2]);height=mix.outputs[0]
  link(ramp(height,(.012,.015,.018),(.075,.085,.1)),shader.inputs['Base Color']);shader.inputs['Metallic'].default_value=.45;shader.inputs['Coat Weight'].default_value=.45
 elif name=='Diamond Plate Plastic':
  mapping_node.inputs['Rotation'].default_value[2]=math.pi/4
  waves=[]
  for axis in ['X','Y']:
   wave=node('TexWave');wave.bands_direction=axis;wave.inputs['Scale'].default_value=6;link(uv,wave.inputs['Vector']);waves.append(mathnode('POWER',wave.outputs['Fac'],4 if axis=='X' else 12))
  height=mathnode('MULTIPLY',*waves);distance=.055;strength=.65
 elif name=='Reptile Scales':
  vor=node('TexVoronoi','Scale boundaries');vor.feature='DISTANCE_TO_EDGE';vor.inputs['Scale'].default_value=30;vor.inputs['Randomness'].default_value=.3;link(uv,vor.inputs['Vector']);height=mathnode('MINIMUM',mathnode('MULTIPLY',vor.outputs['Distance'],8),1)
  link(ramp(height,(.025,.045,.012),base),shader.inputs['Base Color']);distance=.025;strength=.45
 elif name in ['Gold','Copper','Polished Iron']:
  strength=.025;distance=.001
 else:
  link(ramp(noise.outputs['Fac'],tuple(c*.8 for c in base),tuple(c*1.05 for c in base)),shader.inputs['Base Color'])
 bump=node('Bump','Surface relief');bump.inputs['Strength'].default_value=strength;bump.inputs['Distance'].default_value=distance;link(height,bump.inputs['Height']);link(bump.outputs[0],shader.inputs['Normal'])
# Save an editable graph with original external files; importer retains image originals.
bpy.ops.wm.save_as_mainfile(filepath=str(out/(slug+'.blend')))
bpy.ops.wm.obj_import(filepath=mesh)
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
points=[o.matrix_world@Vector(c) for o in objects for c in o.bound_box]
lo=Vector(tuple(min(p[i] for p in points) for i in range(3)));hi=Vector(tuple(max(p[i] for p in points) for i in range(3)))
center=(lo+hi)/2;size=max(hi-lo)
for obj in objects:
 obj.data.materials.clear();obj.data.materials.append(m)
 for poly in obj.data.polygons:poly.use_smooth=True
scene=bpy.context.scene
world=bpy.data.worlds.new('Studio');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.18,.18,1);scene.world=world
# Broad studio panels give metals legible reflections without external render assets.
for pos,power,scale in [((-2,-3,4),700,3),((3,0,2),550,2),((0,3,3),900,2)]:
 data=bpy.data.lights.new('Softbox','AREA');data.energy=power;data.shape='DISK';data.size=size*scale/2
 obj=bpy.data.objects.new('Softbox',data);scene.collection.objects.link(obj);obj.location=center+Vector(pos)*size;obj.rotation_euler=(center-obj.location).to_track_quat('-Z','Y').to_euler()
data=bpy.data.cameras.new('Preview');camera=bpy.data.objects.new('Preview',data);scene.collection.objects.link(camera);scene.camera=camera
camera.location=center+Vector((1.4,-2.4,1.45))*size;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();data.type='ORTHO';data.ortho_scale=size*1.24
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.threads_mode='FIXED';scene.render.threads=4;scene.render.resolution_x=256;scene.render.resolution_y=256;scene.render.resolution_percentage=100
scene.render.film_transparent=True;scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.filepath=str(out/(slug+'.png'))
bpy.ops.render.render(write_still=True)
print('POLYHAVEN_GENERATED',slug,flush=True)
