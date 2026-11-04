"""Own retained Add/Multiply graphs, shared fanout, binary-exact operands; no images."""
import bpy
import json
from pathlib import Path
import sys
out = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
bpy.ops.wm.read_factory_settings(use_empty=True)
for operation, operand in [("ADD", (-.125,.25,.375)), ("MULTIPLY", (.5,.75,1.25))]:
    label = "AW Vector " + operation.title()
    group = bpy.data.node_groups.new(label + " Group", "ShaderNodeTree")
    for name in ("Mapping A", "Mapping B"):
        group.interface.new_socket(name=name, in_out="INPUT", socket_type="NodeSocketVector")
    group.interface.new_socket(name="Shader", in_out="OUTPUT", socket_type="NodeSocketShader")
    inputs, outputs = group.nodes.new("NodeGroupInput"), group.nodes.new("NodeGroupOutput")
    shader, separate = group.nodes.new("ShaderNodeBsdfPrincipled"), group.nodes.new("ShaderNodeSeparateXYZ")
    group.links.new(inputs.outputs["Mapping A"], shader.inputs["Base Color"])
    group.links.new(inputs.outputs["Mapping B"], separate.inputs["Vector"])
    group.links.new(separate.outputs["X"], shader.inputs["Roughness"])
    group.links.new(shader.outputs["BSDF"], outputs.inputs["Shader"])
    material = bpy.data.materials.new(label); material.use_nodes=True; material.use_fake_user=True
    tree=material.node_tree; tree.nodes.clear()
    uv=tree.nodes.new("ShaderNodeTexCoord")
    math=tree.nodes.new("ShaderNodeVectorMath"); math.operation=operation
    constant=tree.nodes.new("ShaderNodeCombineXYZ")
    for i, x in enumerate(operand): constant.inputs[i].default_value=x
    top=tree.nodes.new("ShaderNodeGroup"); top.node_tree=group
    output=tree.nodes.new("ShaderNodeOutputMaterial")
    tree.links.new(uv.outputs["UV"], math.inputs[0]); tree.links.new(constant.outputs["Vector"], math.inputs[1])
    for name in ("Mapping A", "Mapping B"): tree.links.new(math.outputs["Vector"], top.inputs[name])
    tree.links.new(top.outputs["Shader"], output.inputs["Surface"])
out.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(out), relative_remap=False)
(out.with_suffix(".json")).write_text(json.dumps({"kind":"owned-retained-vector-math-v1","materials":["AW Vector Add","AW Vector Multiply"],"operations":["ADD","MULTIPLY"],"images":0,"mapping_order":"retained operation, then selected group input POINT"},indent=2)+"\n")
