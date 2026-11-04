"""Own Blender source fixture: two separately linked vector group inputs."""
import bpy
import os
import pathlib
import sys

destination = pathlib.Path(sys.argv[sys.argv.index("--") + 1]).resolve()
bpy.ops.wm.read_factory_settings(use_empty=True)
group = bpy.data.node_groups.new("Independent linked coordinates", "ShaderNodeTree")
first = group.interface.new_socket(name="Mapping A", in_out="INPUT", socket_type="NodeSocketVector")
second = group.interface.new_socket(name="Mapping B", in_out="INPUT", socket_type="NodeSocketVector")
if os.environ.get("BTS_NATIVE_MAPPING_UPSTREAM"):
    unused = group.interface.new_socket(name="Mapping C", in_out="INPUT", socket_type="NodeSocketVector")
output = group.interface.new_socket(name="Shader", in_out="OUTPUT", socket_type="NodeSocketShader")
inputs = group.nodes.new("NodeGroupInput")
outputs = group.nodes.new("NodeGroupOutput")
shader = group.nodes.new("ShaderNodeBsdfPrincipled")
separate = group.nodes.new("ShaderNodeSeparateXYZ")
group.links.new(inputs.outputs["Mapping A"], shader.inputs["Base Color"])
group.links.new(inputs.outputs["Mapping B"], separate.inputs["Vector"])
group.links.new(separate.outputs["X"], shader.inputs["Roughness"])
group.links.new(shader.outputs["BSDF"], outputs.inputs["Shader"])
material = bpy.data.materials.new("Linked Mapping fixture")
material.use_nodes = True
material.use_fake_user = True
tree = material.node_tree
tree.nodes.clear()
coordinates = tree.nodes.new("ShaderNodeTexCoord")
top = tree.nodes.new("ShaderNodeGroup")
top.node_tree = group
surface = tree.nodes.new("ShaderNodeOutputMaterial")
source = coordinates.outputs["UV"]
if os.environ.get("BTS_NATIVE_MAPPING_UPSTREAM"):
    mapping = tree.nodes.new("ShaderNodeMapping")
    mapping.vector_type = "POINT"
    mapping.inputs["Location"].default_value = (-0.4, 0.0, 0.0)
    mapping.inputs["Scale"].default_value = (1.7, 1.0, 1.0)
    location = tree.nodes.new("ShaderNodeCombineXYZ")
    location.inputs["X"].default_value = 0.125
    scale = tree.nodes.new("ShaderNodeCombineXYZ")
    scale.inputs["X"].default_value = 0.5
    scale.inputs["Y"].default_value = scale.inputs["Z"].default_value = 1.0
    tree.links.new(location.outputs["Vector"], mapping.inputs["Location"])
    tree.links.new(scale.outputs["Vector"], mapping.inputs["Scale"])
    tree.links.new(source, mapping.inputs["Vector"])
    source = mapping.outputs["Vector"]
tree.links.new(source, top.inputs["Mapping A"])
tree.links.new(source, top.inputs["Mapping B"])
if os.environ.get("BTS_NATIVE_MAPPING_UPSTREAM"):
    tree.links.new(source, top.inputs["Mapping C"])
tree.links.new(top.outputs["Shader"], surface.inputs["Surface"])
destination.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(destination))
