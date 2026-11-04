"""Validate and resave a packed custom environment with unmodified Blender."""
import bpy,hashlib,json,sys
from pathlib import Path
out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene;image=scene['bts_environment'];assert image.packed_file
fingerprint=hashlib.sha256(image.packed_file.data).hexdigest();assert fingerprint==scene['bts_environment_sha256']
output=next(n for n in scene.world.node_tree.nodes if n.bl_idname=='ShaderNodeOutputWorld' and n.is_active_output)
background=output.inputs['Surface'].links[0].from_node;assert background.bl_idname=='ShaderNodeBackground'
environment=background.inputs['Color'].links[0].from_node
assert environment.bl_idname=='ShaderNodeTexEnvironment' and environment.image==image
result={'name':scene['bts_environment_name'],'sha256':fingerprint,'bytes':len(image.packed_file.data),
    'native_world_image':True,'version':bpy.app.version_string,'build_hash':bpy.app.build_hash.decode()}
bpy.ops.wm.save_as_mainfile(filepath=str(out/'stock.blend'))
scene['bts_environment_sha256']='0'*64
bpy.ops.wm.save_as_mainfile(filepath=str(out/'bad-hash.blend'))
(out/'stock.json').write_text(json.dumps(result,indent=2)+'\n');print('BTS_STOCK_ENVIRONMENT_PASS')
