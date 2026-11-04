#include "ResourceBudget.hh"
#include "SceneSnapshot.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include "BKE_attribute.hh"
#include "BKE_context.hh"
#include "BKE_idprop.hh"
#include "DNA_camera_types.h"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_object.hh"
#include "BLI_listbase.h"
#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"

namespace bts {
namespace {
using namespace blender;

const bNode *eevee_output(const bNodeTree &tree)
{
  // Read-only target precedence from ntreeShaderOutputNode at the pinned Blender revision.
  const bNode *output = nullptr;
  for (const bNode &node : tree.nodes) {
    if (!STREQ(node.idname, "ShaderNodeOutputMaterial")) {
      continue;
    }
    const bool prefer_active = output && (node.flag & NODE_DO_OUTPUT) &&
                               !(output->flag & NODE_DO_OUTPUT);
    if (node.custom1 == SHD_OUTPUT_ALL) {
      if (!output || (output->custom1 == SHD_OUTPUT_ALL && prefer_active)) {
        output = &node;
      }
    }
    else if (node.custom1 == SHD_OUTPUT_EEVEE &&
             (!output || output->custom1 == SHD_OUTPUT_ALL || prefer_active)) {
      output = &node;
    }
  }
  return output;
}

const bNode *direct_principled_surface(const Material &material)
{
  if (!material.nodetree) {
    return nullptr;
  }
  const bNode *principled = nullptr;
  for (const bNode &node : material.nodetree->nodes) {
    if (STREQ(node.idname, "ShaderNodeBsdfPrincipled")) {
      if (principled) {
        return nullptr;
      }
      principled = &node;
    }
  }
  const bNode *output = eevee_output(*material.nodetree);
  if (!principled || !output || (principled->flag & NODE_MUTED) || (output->flag & NODE_MUTED)) {
    return nullptr;
  }
  for (const bNodeLink &link : material.nodetree->links) {
    if (link.fromnode == principled && link.tonode == output && link.tosock && link.fromsock &&
        (link.flag & NODE_LINK_VALID) && !(link.flag & NODE_LINK_MUTED) &&
        !(link.fromsock->flag & SOCK_UNAVAIL) && !(link.tosock->flag & SOCK_UNAVAIL) &&
        STREQ(link.tosock->identifier, "Surface")) {
      return principled;
    }
  }
  // Only our known visibility wrapper may precede the host's original view switch.
  const bNode *view=nullptr;
  for(const bNodeLink &outer:material.nodetree->links)if(outer.tonode==output&&outer.tosock&&STREQ(outer.tosock->identifier,"Surface")&&(outer.flag&NODE_LINK_VALID)&&!(outer.flag&NODE_LINK_MUTED)&&outer.fromnode&&!(outer.fromnode->flag&NODE_MUTED)){
    if(STREQ(outer.fromnode->name,"__BTS_ViewSwitch")&&STREQ(outer.fromnode->idname,"ShaderNodeMixShader"))view=outer.fromnode;
    else if(STREQ(outer.fromnode->name,"__BTS_GeometrySwitch")&&STREQ(outer.fromnode->idname,"ShaderNodeMixShader")){
      int i=0;const bNodeSocket *original=nullptr;for(const auto &socket:outer.fromnode->inputs)if(i++==1)original=&socket;
      for(const auto &link:material.nodetree->links)if(link.tonode==outer.fromnode&&link.tosock==original&&link.fromnode&&STREQ(link.fromnode->name,"__BTS_ViewSwitch")&&STREQ(link.fromnode->idname,"ShaderNodeMixShader")&&(link.flag&NODE_LINK_VALID)&&!(link.flag&NODE_LINK_MUTED)&&!(link.fromnode->flag&NODE_MUTED))view=link.fromnode;
    }
  }
  if(view){int i=0;const bNodeSocket *original=nullptr;for(const auto &socket:view->inputs)if(i++==1)original=&socket;for(const auto &link:material.nodetree->links)if(link.fromnode==principled&&link.tonode==view&&link.tosock==original&&(link.flag&NODE_LINK_VALID)&&!(link.flag&NODE_LINK_MUTED)&&link.fromsock&&!(link.fromsock->flag&SOCK_UNAVAIL)&&!(link.tosock->flag&SOCK_UNAVAIL))return principled;}
  return nullptr;
}

const bNodeSocket *constant_input(const bNode &node, const char *identifier, int socket_type)
{
  for (const bNodeSocket &socket : node.inputs) {
    if (STREQ(socket.identifier, identifier) && socket.type == socket_type && socket.default_value &&
        !(socket.flag & (SOCK_IS_LINKED | SOCK_UNAVAIL))) {
      return &socket;
    }
  }
  return nullptr;
}

struct RoughnessAccess {
  const bNodeSocket *socket = nullptr;
  std::string unavailable_reason;
};

RoughnessAccess roughness_access(const Main &main, const Material *material)
{
  if (!material) {
    return {nullptr, "No material is assigned to this texture set."};
  }
  if (ID_IS_LINKED(&material->id)) {
    return {nullptr, "This material is linked from another file and is read-only."};
  }
  if (ID_IS_OVERRIDE_LIBRARY(&material->id) || !BKE_id_is_editable(&main, &material->id)) {
    return {nullptr, "This material is read-only in this workspace."};
  }
  if (material->id.tag & (ID_TAG_NO_MAIN | ID_TAG_LOCALIZED | ID_TAG_COPIED_ON_EVAL | ID_TAG_MISSING)) {
    return {nullptr, "This material is not an editable material in the project."};
  }
  if (material->nodetree && material->nodetree->adt) {
    return {nullptr, "This shader has animation or drivers; direct roughness editing is unavailable."};
  }
  const bNode *principled = direct_principled_surface(*material);
  if (!principled) {
    return {nullptr, "Roughness editing requires one active Principled shader connected directly "
                     "to the material's EEVEE Surface output."};
  }
  const bNodeSocket *socket = constant_input(*principled, "Roughness", SOCK_FLOAT);
  if (!socket) {
    return {nullptr, "Roughness is connected to another input or is unavailable."};
  }
  if (!std::isfinite(static_cast<const bNodeSocketValueFloat *>(socket->default_value)->value)) {
    return {nullptr, "This material's roughness value is invalid."};
  }
  return {socket, {}};
}

}  // namespace

blender::bNode *native_principled_surface(blender::Material &material)
{
  return const_cast<blender::bNode *>(direct_principled_surface(material));
}
blender::bNode *native_material_output(blender::Material &material)
{
  return material.nodetree ? const_cast<blender::bNode *>(eevee_output(*material.nodetree)) : nullptr;
}

MaterialSummary read_material_summary(const blender::Main &main,
                                      const blender::Material *material,
                                      const blender::Scene &scene)
{
  using namespace blender;
  MaterialSummary result;
  std::array<float, 4> linear{0.8f, 0.8f, 0.8f, 1.0f};
  result.name = "Unassigned";
  if (material) {
    const Material *original = DEG_get_original(material);
    result.session_id = original->id.session_uid;
    result.name = original->id.name + 2;
    linear = {material->r, material->g, material->b, material->a};
    result.roughness = material->roughness;
    if (const bNode *principled = direct_principled_surface(*material)) {
      if (const bNodeSocket *socket = constant_input(*principled, "Base Color", SOCK_RGBA)) {
        const auto *value = static_cast<const bNodeSocketValueRGBA *>(socket->default_value);
        std::copy_n(value->value, 4, linear.begin());
        result.constant_base_color = true;
      }
      if (const bNodeSocket *socket = constant_input(*principled, "Roughness", SOCK_FLOAT)) {
        const float value = static_cast<const bNodeSocketValueFloat *>(socket->default_value)->value;
        if (std::isfinite(value)) {
          result.roughness = value;
          result.constant_roughness = true;
        }
      }
    }
  }
  result.values.base_color = linear;
  result.values.roughness = result.roughness;
  if (material) {
    result.values.metallic = material->metallic;
    if (const bNode *principled = direct_principled_surface(*material)) {
      if (const bNodeSocket *socket = constant_input(*principled, "Metallic", SOCK_FLOAT)) {
        result.values.metallic = static_cast<const bNodeSocketValueFloat *>(socket->default_value)->value;
        result.uniform_editable = result.constant_base_color && result.constant_roughness &&
            constant_input(*principled, "Normal", SOCK_VECTOR);
      }
    }
  }
  const RoughnessAccess access = roughness_access(main, material ? DEG_get_original(material) : nullptr);
  result.roughness_editable = result.constant_roughness && access.socket;
  result.uniform_editable &= bool(access.socket);
  result.roughness_unavailable_reason = access.unavailable_reason;
  if (!result.roughness_editable && result.roughness_unavailable_reason.empty()) {
    result.roughness_unavailable_reason = "The evaluated material has no known constant roughness.";
  }
  IMB_colormanagement_pixel_to_display_space_v4(result.display_base_color.data(), linear.data(),
                                                &scene.view_settings, &scene.display_settings);
  return result;
}

void write_material_roughness(blender::Main &main, blender::Material &material, float roughness)
{
  using namespace blender;
  if (!std::isfinite(roughness)) {
    throw std::invalid_argument("Material roughness must be finite");
  }
  const RoughnessAccess access = roughness_access(main, &material);
  if (!access.socket) {
    throw std::runtime_error(access.unavailable_reason);
  }
  auto *socket = const_cast<bNodeSocket *>(access.socket);
  static_cast<bNodeSocketValueFloat *>(socket->default_value)->value = std::clamp(roughness, 0.0f, 1.0f);
  BKE_ntree_update_tag_socket_property(material.nodetree, socket);
  BKE_ntree_update_after_single_tree_change(main, *material.nodetree);
  DEG_id_tag_update_ex(&main, &material.id, ID_RECALC_SHADING);
  DEG_id_tag_update_ex(&main, &material.nodetree->id, ID_RECALC_NTREE_OUTPUT);
}

void write_material_values(blender::Main &main, blender::Material &material, const MaterialValues &values)
{
  using namespace blender;
  const auto roughness = roughness_access(main, &material);
  const bNode *node = direct_principled_surface(material);
  const bNodeSocket *color = node ? constant_input(*node, "Base Color", SOCK_RGBA) : nullptr;
  const bNodeSocket *metal = node ? constant_input(*node, "Metallic", SOCK_FLOAT) : nullptr;
  const bNodeSocket *normal = node ? constant_input(*node, "Normal", SOCK_VECTOR) : nullptr;
  if (!roughness.socket || !color || !metal || !normal) {
    throw std::runtime_error("Channel editing currently requires a local constant Principled material");
  }
  for (float value : values.base_color)
    if (!std::isfinite(value) || value < 0 || value > 65504) throw std::invalid_argument("Invalid linear color");
  if (!std::isfinite(values.metallic) || values.metallic < 0 || values.metallic > 1 ||
      !std::isfinite(values.roughness) || values.roughness < 0 || values.roughness > 1)
    throw std::invalid_argument("Invalid material scalar");
  // Resolve and validate every destination before changing any authored native input.
  std::copy(values.base_color.begin(), values.base_color.end(),
            static_cast<bNodeSocketValueRGBA *>(color->default_value)->value);
  static_cast<bNodeSocketValueFloat *>(metal->default_value)->value = values.metallic;
  static_cast<bNodeSocketValueFloat *>(roughness.socket->default_value)->value = values.roughness;
  for (const auto *socket : {color, metal, roughness.socket})
    BKE_ntree_update_tag_socket_property(material.nodetree, const_cast<bNodeSocket *>(socket));
  BKE_ntree_update_after_single_tree_change(main, *material.nodetree);
  DEG_id_tag_update_ex(&main, &material.id, ID_RECALC_SHADING);
  DEG_id_tag_update_ex(&main, &material.nodetree->id, ID_RECALC_NTREE_OUTPUT);
}

SceneSnapshot read_scene_snapshot(blender::bContext *context,
                                 const blender::View3D *view,
                                 bool include_unmapped,const SourceMeshMap *source_meshes)
{
  using namespace blender;
  Main *main = CTX_data_main(context);
  Scene *scene = CTX_data_scene(context);
  ViewLayer *layer = CTX_data_view_layer(context);
  Depsgraph *graph = CTX_data_ensure_evaluated_depsgraph(context);
  BKE_view_layer_synced_ensure(*main, scene, layer);
  SceneSnapshot result;
  std::unordered_map<const Material *, std::size_t> material_indices;
  auto material_index = [&](const Material *material) {
    const Material *original = material ? DEG_get_original(material) : nullptr;
    const auto [it, inserted] = material_indices.try_emplace(original, result.materials.size());
    if (inserted) {
      result.materials.push_back(read_material_summary(*main, material, *scene));
    }
    return it->second;
  };
  for (Base &base : *BKE_view_layer_object_bases_get(layer)) {
    if(!BKE_base_is_visible(view,&base))continue;
    Object *object=DEG_get_evaluated(graph,base.object);
    if(object->type==OB_CAMERA&&object->data){auto &camera=*reinterpret_cast<Camera *>(object->data);SceneCamera entry;entry.session_id=DEG_get_original(object)->id.session_uid;entry.name=DEG_get_original(object)->id.name+2;const auto &matrix=object->object_to_world();for(int axis=0;axis<3;++axis){entry.position[axis]=matrix[3][axis];entry.forward[axis]=-matrix[2][axis];entry.up[axis]=matrix[1][axis];}entry.vertical_fov=float(2*std::atan(camera.sensor_y/(2*camera.lens))*180/3.14159265358979323846);entry.near_clip=camera.clip_start;entry.far_clip=camera.clip_end;entry.orthographic=camera.type==CAM_ORTHO;entry.orthographic_extent=camera.ortho_scale;const auto *group=IDP_GetProperties(&camera.id);const auto *property=group?IDP_GetPropertyFromGroup(group,"bts_imported_aspect"):nullptr;if(property&&property->type==IDP_DOUBLE)entry.aspect=float(IDP_double_get(property));result.cameras.push_back(entry);continue;}
    if(object->type!=OB_MESH)continue;
    if (!(BKE_object_visibility(object, DAG_EVAL_VIEWPORT) & OB_VISIBLE_SELF)) {
      continue;
    }
    const Mesh *mesh = BKE_object_get_evaluated_mesh(object);
    if(source_meshes){const auto found=source_meshes->find(DEG_get_original(object)->id.session_uid);if(found!=source_meshes->end())mesh=found->second;}
    if (!mesh) {
      continue;
    }
    ++result.mesh_objects;result.object_names[DEG_get_original(object)->id.session_uid]=DEG_get_original(object)->id.name+2;result.object_vertex_counts[DEG_get_original(object)->id.session_uid]=mesh->verts_num;
    const auto positions = mesh->vert_positions();
    for (const float3 &position : positions) {
      const float3 world = math::transform_point(object->object_to_world(), position);
      if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
        continue;
      }
      for (int axis = 0; axis < 3; ++axis) {
        result.minimum[axis] = result.has_bounds ? std::min(result.minimum[axis], world[axis]) : world[axis];
        result.maximum[axis] = result.has_bounds ? std::max(result.maximum[axis], world[axis]) : world[axis];
      }
      result.has_bounds = true;
    }
    const int slots = BKE_object_material_count_eval(*object, mesh->id);
    const auto attributes = mesh->attributes();
    const VArraySpan<int> face_materials = *attributes.lookup_or_default<int>(
        "material_index", bke::AttrDomain::Face, 0);
    const auto faces = mesh->faces();
    const auto unused_slot = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> slot_indices(std::max(1, slots), unused_slot);
    for (const int face : faces.index_range()) {
      const int slot = std::clamp(face_materials[face], 0, int(slot_indices.size()) - 1);
      if (slot_indices[slot] == unused_slot) {
        slot_indices[slot] = material_index(
            slot < slots ? BKE_object_material_get_eval(*object, mesh->id, short(slot + 1)) : nullptr);
      }
    }
    const auto uv_name = mesh->active_or_default_uv_map_name();
    const VArraySpan<float2> uv=uv_name.is_empty()?VArraySpan<float2>{}:VArraySpan<float2>(*attributes.lookup<float2>(uv_name,bke::AttrDomain::Corner));
    if(uv.is_empty()){++result.meshes_without_uvs;if(!include_unmapped)continue;}
    const auto triangles = mesh->corner_tris();
    const auto triangle_faces = mesh->corner_tri_faces();
    require_texture_ram(resource_product(resource_sum(result.uv_triangles.size(),triangles.size()),sizeof(UvFaceTriangle)),"UV scene snapshot");
    result.uv_triangles.reserve(result.uv_triangles.size() + triangles.size());
    const auto corner_normals=mesh->corner_normals();const auto normal_to_world=math::transpose(float3x3(object->world_to_object()));
    for (const int index : triangles.index_range()) {
      const int3 corners = triangles[index];
      const int face_index = triangle_faces[index];
      const IndexRange face = faces[face_index];
      const int slot = std::clamp(face_materials[face_index], 0, int(slot_indices.size()) - 1);
      UvFaceTriangle triangle;
      triangle.material = slot_indices[slot];triangle.has_normals=true;
      const auto object_id=std::uint64_t(DEG_get_original(object)->id.session_uid)<<32;
      triangle.triangle_id=object_id|std::uint32_t(index);triangle.polygon_id=object_id|std::uint32_t(face_index);
      bool finite = true;
      for (int i = 0; i < 3; ++i) {
        const float2 coordinate = uv.is_empty()?float2(0):uv[corners[i]];
        finite &= std::isfinite(coordinate.x) && std::isfinite(coordinate.y);
        triangle.corners[i] = {coordinate.x, coordinate.y};
        const int vertex=mesh->corner_verts()[corners[i]];
        const auto world=math::transform_point(object->object_to_world(),positions[vertex]);
        triangle.positions[i]={world.x,world.y,world.z};
        const auto normal=math::normalize(normal_to_world*corner_normals[corners[i]]);triangle.normals[i]={normal.x,normal.y,normal.z};for(int axis=0;axis<3;++axis)finite&=std::isfinite(normal[axis]);
        triangle.vertex_ids[i]=(std::uint64_t(DEG_get_original(object)->id.session_uid)<<32)|std::uint32_t(vertex);
        const int a = corners[i], b = corners[(i + 1) % 3];
        if (std::abs(a - b) == 1 ||
            (std::min(a, b) == face.start() && std::max(a, b) == face.last())) {
          triangle.boundary_edges |= 1 << i;
        }
      }
      if (finite) {
        result.uv_triangles.push_back(triangle);
      }
    }
  }
  // Deterministic material-defined set order. Remap UV triangle indices along with summaries.
  std::vector<std::size_t> order(result.materials.size()), remap(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
    return result.materials[a].name < result.materials[b].name;
  });
  auto unsorted = std::move(result.materials);
  result.materials.reserve(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    remap[order[i]] = i;
    result.materials.push_back(std::move(unsorted[order[i]]));
  }
  for (auto &triangle : result.uv_triangles) triangle.material = remap[triangle.material];
  return result;
}

}  // namespace bts
