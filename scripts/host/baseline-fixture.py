"""Generate and render an isolated source-build fixture, without user preferences."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

import bpy
import gpu
from mathutils import Vector

project = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(project / "src"))
from blender_texture_studio.runtime_color import initialize_native_acescg

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
args.output.mkdir(parents=True, exist_ok=True)
color = initialize_native_acescg()
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.eevee.taa_samples = 8
scene.eevee.taa_render_samples = 32
scene.render.resolution_x, scene.render.resolution_y = 640, 480
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"

material = bpy.data.materials.new("Copper")
material.use_nodes = True
bsdf = material.node_tree.nodes.get("Principled BSDF")
bsdf.inputs["Base Color"].default_value = (0.32, 0.13, 0.055, 1)
bsdf.inputs["Metallic"].default_value = 0.8
bsdf.inputs["Roughness"].default_value = 0.18
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, location=(-1.15, 0, 0))
sphere = bpy.context.object
sphere.name = "UV sphere"
for polygon in sphere.data.polygons:
    polygon.use_smooth = True
sphere.data.materials.append(material)
bpy.ops.mesh.primitive_cube_add(size=1.6, location=(1.25, 0, -0.15))
cube = bpy.context.object
cube.name = "Rounded cube"
bevel = cube.modifiers.new("Rounded edges", "BEVEL")
bevel.width, bevel.segments = 0.15, 5
cube.data.materials.append(material)

camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
scene.collection.objects.link(camera)
camera.location = (4, -8, 3.2)
camera.rotation_euler = (-Vector(camera.location)).to_track_quat("-Z", "Y").to_euler()
camera.data.type, camera.data.ortho_scale = "ORTHO", 5.8
scene.camera = camera
for name, position, energy, size in [
    ("Key", (-3, -4, 5), 1200, 4),
    ("Rim", (3, 1, 4), 1600, 3),
    ("Fill", (1, -4, 0), 350, 2),
]:
    lamp = bpy.data.lights.new(name, "AREA")
    lamp.energy, lamp.shape, lamp.size = energy, "DISK", size
    obj = bpy.data.objects.new(name, lamp)
    scene.collection.objects.link(obj)
    obj.location = position
    obj.rotation_euler = (-Vector(position)).to_track_quat("-Z", "Y").to_euler()
scene.world.color = (0.15, 0.15, 0.15)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            space = area.spaces.active
            space.shading.type = "MATERIAL"
            space.overlay.show_overlays = False
            space.region_3d.view_distance = 7
            space.region_3d.view_location = (0, 0, 0)
            space.region_3d.view_rotation = camera.rotation_euler.to_quaternion()

bpy.ops.wm.save_as_mainfile(filepath=str(args.output / "host-fixture.blend"))
frames = []
for roughness in (0.18, 0.8):
    bsdf.inputs["Roughness"].default_value = roughness
    scene.render.filepath = str(args.output / f"roughness-{roughness}.png")
    assert bpy.ops.render.render(write_still=True) == {"FINISHED"}
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    values = list(image.pixels)
    rgb = [value for i, value in enumerate(values) if i % 4 != 3]
    assert max(rgb) - min(rgb) > 0.05, "Blank EEVEE render"
    frames.append(values)
    bpy.data.images.remove(image)
difference = sum(abs(a - b) for a, b in zip(*frames)) / len(frames[0])
assert difference > 0.002, "Roughness did not affect the rendered image"
gpu.init()
report = {
    "source": bpy.app.build_hash.decode(),
    "version": bpy.app.version_string,
    "pid": os.getpid(),
    "engine": scene.render.engine,
    "gpu": gpu.platform.renderer_get(),
    "backend": gpu.platform.backend_type_get(),
    "color": color,
    "roughness_mean_difference": difference,
    "fixture_sha256": hashlib.sha256((args.output / "host-fixture.blend").read_bytes()).hexdigest(),
}
assert "3090" in report["gpu"], report["gpu"]
(args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
print("BTS_BASELINE_VERIFIED " + json.dumps(report), flush=True)
