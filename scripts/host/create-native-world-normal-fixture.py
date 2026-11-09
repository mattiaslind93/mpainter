# SPDX-License-Identifier: GPL-3.0-or-later
"""Own image-free native Bump and flat sources plus a unique-UV two-UDIM mesh.

Run only in the approved background Blender fixture process, with an absolute
.blend output after --. This script is fixture creation, never host acceptance.
"""
import bpy
import hashlib
import json
from pathlib import Path
import sys

output = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
assert output.suffix == ".blend" and not output.exists()
output.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)

for perturbed, raw_eligible, label in ((False, False, "AW World Flat"),
                                      (True, False, "AW World Bump"),
                                      (True, True, "AW World Raw Eligible")):
    group = bpy.data.node_groups.new(label + " Group", "ShaderNodeTree")
    controls = [("Color Level", .25, 1.0), ("Roughness", .4, 1.0)]
    controls += ([("Height Gain", 1.0, 2.0)] if raw_eligible else
                 [("Strength", .7, 1.0), ("Distance", .12, 1.0)])
    for name, value, maximum in controls:
        socket = group.interface.new_socket(name=name, in_out="INPUT", socket_type="NodeSocketFloat")
        socket.default_value = value
        socket.min_value = 0.0
        socket.max_value = maximum
    group.interface.new_socket(name="Vector", in_out="INPUT", socket_type="NodeSocketVector")
    group.interface.new_socket(name="Shader", in_out="OUTPUT", socket_type="NodeSocketShader")
    inputs = group.nodes.new("NodeGroupInput")
    outputs = group.nodes.new("NodeGroupOutput")
    surface = group.nodes.new("ShaderNodeBsdfPrincipled")
    group.links.new(inputs.outputs["Color Level"], surface.inputs["Base Color"])
    group.links.new(inputs.outputs["Roughness"], surface.inputs["Roughness"])
    if perturbed:
        separate = group.nodes.new("ShaderNodeSeparateXYZ")
        square = group.nodes.new("ShaderNodeMath")
        square.operation = "MULTIPLY"
        bump = group.nodes.new("ShaderNodeBump")
        bump.inputs["Filter Width"].default_value = .1
        group.links.new(inputs.outputs["Vector"], separate.inputs["Vector"])
        for index in (0, 1):
            group.links.new(separate.outputs["X"], square.inputs[index])
        height = square.outputs["Value"]
        if raw_eligible:
            gain = group.nodes.new("ShaderNodeMath")
            gain.operation = "MULTIPLY"
            group.links.new(height, gain.inputs[0])
            group.links.new(inputs.outputs["Height Gain"], gain.inputs[1])
            height = gain.outputs["Value"]
            # Existing RawH admission requires literal, unlinked Bump controls.
            # Keep that production policy; do not reuse the linked main source.
            bump.inputs["Strength"].default_value = .7
            bump.inputs["Distance"].default_value = .12
        else:
            for name in ("Strength", "Distance"):
                group.links.new(inputs.outputs[name], bump.inputs[name])
        group.links.new(height, bump.inputs["Height"])
        group.links.new(bump.outputs["Normal"], surface.inputs["Normal"])
    group.links.new(surface.outputs["BSDF"], outputs.inputs["Shader"])
    material = bpy.data.materials.new(label)
    material.use_nodes = True
    material.use_fake_user = True
    tree = material.node_tree
    tree.nodes.clear()
    coordinates = tree.nodes.new("ShaderNodeTexCoord")
    source = tree.nodes.new("ShaderNodeGroup")
    source.node_tree = group
    result = tree.nodes.new("ShaderNodeOutputMaterial")
    tree.links.new(coordinates.outputs["UV"], source.inputs["Vector"])
    tree.links.new(source.outputs["Shader"], result.inputs["Surface"])

assert not bpy.data.images
bpy.ops.wm.save_as_mainfile(filepath=str(output), relative_remap=False)
# Each face has unique, non-overlapping UV support and an explicit normal.
# The second plane is tilted: the same tangent perturbation must become a
# different WORLD vector. One named object/material keeps set ownership stable.
mesh = output.with_suffix(".obj")
mesh.write_text("""o AW_World_Owner
v -1.1 -0.5 0
v -0.1 -0.5 0
v -0.1 0.5 0
v -1.1 0.5 0
v 0.1 -0.5 0
v 0.9 -0.5 -0.6
v 0.9 0.5 -0.6
v 0.1 0.5 0
vt 0.125 0.125
vt 0.875 0.125
vt 0.875 0.875
vt 0.125 0.875
vt 1.125 0.125
vt 1.875 0.125
vt 1.875 0.875
vt 1.125 0.875
vn 0 0 1
vn 0.6 0 0.8
usemtl AW_World_Target
s off
f 1/1/1 2/2/1 3/3/1 4/4/1
f 5/5/2 6/6/2 7/7/2 8/8/2
""")
def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()
output.with_suffix(".json").write_text(json.dumps({
    "kind": "owned-native-world-normal-v1",
    "source": str(output), "source_sha256": sha(output),
    "mesh": str(mesh), "mesh_sha256": sha(mesh), "images": 0,
    "materials": ["AW World Flat", "AW World Bump", "AW World Raw Eligible"],
    "groups": ["AW World Flat Group", "AW World Bump Group", "AW World Raw Eligible Group"],
    "tiles": [1001, 1002], "uv_layer": "UVMap",
    "normal_source": "UV.x * UV.x -> Bump.Height -> Principled.Normal",
    "filter_width": .1, "strength": .7, "distance": .12,
    "raw_eligible_source": "AW World Raw Eligible: H=UV.x²*Height Gain, Bump Strength/Distance/Filter Width literal and unlinked",
    "obj_face_normals": [[0, 0, 1], [.6, 0, .8]],
    "imported_world_face_normals": [[0, -1, 0], [.6, -.8, 0]],
    "obj_import_axis_matrix_rows": [[1, 0, 0], [0, 0, -1], [0, 1, 0]],
    "axis_reference": "Pinned OBJImportParams -Z forward/Y up; importer transform_object and BLI axis lookup 0x351, column-major matrix21",
    "chart_bounds_local": [.125, .125, .875, .875],
    "edge_probe": "at resolution/8-1 and resolution/8, y=resolution/2 on both tiles; outside associated vector+A must equal its inside margin donor",
    "scope": "Fixture source only. Actual production import and GPU/UI acceptance are separate."
}, indent=2) + "\n")
