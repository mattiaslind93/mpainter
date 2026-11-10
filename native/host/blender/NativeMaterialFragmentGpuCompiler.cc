// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSurfaceBasis.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "NativeMaterialFragmentContext.hh"
#include "NativeMaterialProjection.hh"
#include "NativeMaterialProjectionSource.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialRamp.hh"
#include "NativeFragmentGeometry.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "NativeMaterialFragmentImages.hh"
#include "NativeMaterialSharedGpuImages.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeFragmentImageReadiness.hh"
#include "NativeMaterialSampler.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "DNA_userdef_types.h"
#include "DNA_image_types.h"
#include "ResourceAvailability.hh"
#include "BKE_colorband.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BLI_hash.h"
#include "BLI_listbase.h"
#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "DNA_customdata_types.h"
#include "DNA_node_types.h"
#include "GPU_batch.hh"
#include "GPU_capabilities.hh"
#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
#include "GPU_index_buffer.hh"
#include "GPU_material.hh"
#include "GPU_material_hosted.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_vertex_buffer.hh"
#include "MEM_guardedalloc.h"
#include "NOD_shader.h"
#include "gpu_shader_create_info.hh"
#include "draw_shader_shared.hh"
#include <epoxy/gl.h>
#include <QScopeGuard>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace bts {
namespace {
using namespace blender;
void require(bool ok, const std::string &error)
{
  if (!ok)
    throw std::invalid_argument(error);
}
struct Compilation {
  PreparedNativeFragmentNormal prepared;
  NativeFragmentLayout layout;
  NativeFragmentDiagnostics diagnostic;
  gpu::shader::StageInterfaceInfo varying{"bts_native_fragment_interface"};
  std::string error;
  std::size_t count = 0;
  std::set<const bNode *> required;
};
// Const equivalent of pinned node.cc3030–3044: available socket, display name.
// Blender exposes only a mutable overload; inspection never mutates this graph.
const bNodeSocket *enabled_input(const bNode &node, const char *name)
{
  for (const auto &socket : node.inputs)
    if (socket.is_available() && STREQ(socket.name, name))
      return &socket;
  return nullptr;
}
std::size_t physical(const Compilation &c, std::size_t logical)
{
  require(logical < c.prepared.source->description.parameters.size(),
          "Fragment logical parameter outside source");
  const auto row = c.layout.parameter_slots.empty() ? logical : c.layout.parameter_slots[logical];
  require(row < c.count, "Fragment physical parameter outside layout");
  return row;
}
std::string attribute(const GPUMaterialAttribute &a, const Compilation &c)
{
  const std::string_view name(a.name);
  if (a.type == CD_TANGENT) {
    require(c.layout.tangent_stream && name == c.layout.uv_name,
            "Fragment tangent stream missing or UV name mismatched: " + std::string(name));
    return c.layout.fill_projection ? "bts_projected_tangent" : "fragment_tangent";
  }
  require(a.type == CD_AUTO_FROM_NAME || a.type == CD_ORCO,
          "Fragment attribute type needs an unavailable physical binding: " + std::string(name));
  const bool vec3 = a.type == CD_ORCO;
  const auto value = [&](const char *s) {
    return vec3 ? std::string(s) : "vec4(" + std::string(s) + ",1.0)";
  };
  if (name == "BTS_Dynamic_Zero")
    return vec3 ? "vec3(0)" : "vec4(0)";
  if (const auto mapping = read_native_mapping_attribute(
          name, c.prepared.source->description.parameters.size()))
  {
    const auto row = resource_sum(
        c.count, resource_sum(resource_product(physical(c, mapping->parameter), 3), mapping->row));
    return "native_parameters[" + std::to_string(row) + "]" + (vec3 ? ".xyz" : "");
  }
  if (name.starts_with("BTS_Param")) {
    std::size_t index = 0;
    const auto suffix = name.substr(9);
    const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
    require(parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size(),
            "Invalid fragment parameter name");
    return "native_parameters[" + std::to_string(physical(c, index)) + "]" + (vec3 ? ".xyz" : "");
  }
  if (c.layout.fill_projection) {
    if (name == "BTS_Coord_UV" ||
        (a.type != CD_ORCO && (name.empty() || name == c.layout.uv_name)))
      return value("bts_uv");
    if (name == "BTS_Geometry_Position")
      return value("bts_position");
    if (name == "BTS_Coord_Object")
      return value("bts_object");
    if (name == "BTS_Coord_Generated" || (name.empty() && a.type == CD_ORCO))
      return value("bts_generated");
  }
  if (name.empty() && a.type == CD_ORCO)
    return native_material_fragment_context_value("BTS_Coord_Generated",
                                                  true,
                                                  c.layout.paired_physical_sides,
                                                  false,
                                                  c.layout.generated_stream &&
                                                      c.layout.resident_raster_origin);
  if (name.starts_with("BTS_"))
    return native_material_fragment_context_value(
        name,
        vec3,
        c.layout.paired_physical_sides,
        c.layout.resident_object_context && c.layout.resident_raster_origin,
        c.layout.generated_stream && c.layout.resident_raster_origin,
        c.layout.resident_view_context && c.layout.resident_raster_origin,
        c.layout.resident_baked_ao_context && c.layout.resident_raster_origin);
  // Named/active UV is admitted only as the exact explicitly supplied stream.
  if (!name.starts_with("BTS_") &&
      (name == c.layout.uv_name ||
       (name.empty() && c.layout.default_image_uv && c.layout.resident_images &&
        c.layout.resident_raster_origin)) &&
      a.type != CD_ORCO)
    return value("vec3(fragment_uv,0)");
  throw std::invalid_argument("Fragment attribute has no supplied UV/object/view binding: " +
                              std::string(name));
}
void inspect(Compilation &c)
{
  require(!c.layout.fill_projection || c.layout.resident_raster_origin,
          "Native Fill projection requires resident geometry");
  require(c.prepared.source && c.prepared.source->main && c.prepared.source->material &&
              c.prepared.tree,
          "Fragment prepared owner/tree missing");
  require(BLI_findindex(&c.prepared.source->main->nodetrees, &c.prepared.tree->id) >= 0,
          "Fragment tree is outside its strong Main owner");
  require(
      !c.prepared.source->world_normal_source_displacement ||
          c.prepared.source->world_normal_surface_contract ==
              NativeWorldNormalSurfaceContract::PainterUndisplaced,
      "Fragment authored Displacement requires the explicit effective Painter-undisplaced surface contract");
  if (const auto *surface = ntreeShaderOutputNode(c.prepared.tree, SHD_OUTPUT_EEVEE))
    for (const auto &socket : surface->inputs)
      if (STREQ(socket.identifier, "Displacement"))
        require(!socket.link,
                "Private Normal AOV retained an active MaterialOutput Displacement input");
  const auto logical = c.prepared.source->description.parameters.size();
  c.count = c.layout.physical_parameters ? c.layout.physical_parameters : logical;
  require(c.layout.parameter_slots.empty() || c.layout.parameter_slots.size() == logical,
          "Fragment parameter remap size differs");
  for (std::size_t i = 0; i < logical; ++i)
    physical(c, i);
  require(c.layout.uv_name.size() < MAX_CUSTOMDATA_LAYER_NAME_NO_PREFIX &&
              c.layout.uv_name.find('\0') == std::string::npos &&
              !std::string_view(c.layout.uv_name).starts_with("BTS_"),
          "Invalid fragment UV stream name");
  const bNode *aov = nullptr;
  int roots = 0;
  for (const auto &node : c.prepared.tree->nodes)
    if (STREQ(node.idname, "ShaderNodeOutputAOV")) {
      aov = &node;
      ++roots;
      require(STREQ(static_cast<const NodeShaderOutputAOV *>(node.storage)->name, "BTS_Channel"),
              "Fragment AOV identity differs");
    }
  require(aov && roots == 1, "Fragment needs exactly one prepared Normal AOV");
  std::vector<const bNode *> todo{aov};
  while (!todo.empty()) {
    const auto *node = todo.back();
    todo.pop_back();
    if (!c.required.insert(node).second)
      continue;
    for (const auto &edge : c.prepared.tree->links)
      if (edge.tonode == node)
        todo.push_back(edge.fromnode);
  }
  for (const auto *node : c.required) {
    const std::string_view type(node->idname);
    if (type == "ShaderNodeAttribute") {
      const auto *settings = static_cast<const NodeShaderAttribute *>(node->storage);
      require(settings && settings->type == SHD_ATTRIBUTE_GEOMETRY,
              "Fragment uniform object/instancer/view-layer attributes need a physical binding");
      GPUMaterialAttribute binding{};
      binding.type = CD_AUTO_FROM_NAME;
      STRNCPY(binding.name, settings->name);
      attribute(binding, c);
      c.diagnostic.object_context |= native_material_fragment_object_attribute(settings->name);
      c.diagnostic.object_matrices_context |= native_material_fragment_object_matrices_attribute(
          settings->name);
      c.diagnostic.generated_context |= STREQ(settings->name, "BTS_Coord_Generated");
      c.diagnostic.view_context |= STREQ(settings->name, "BTS_Geometry_Incoming");
      c.diagnostic.baked_ao_context |= STREQ(settings->name, "BTS_Mesh_AO");
    }
    if (type.starts_with("ShaderNodeTex") && type != "ShaderNodeTexImage")
      for (const auto &socket : node->inputs)
        if (STREQ(socket.identifier, "Vector") && !socket.link) {
          if (!native_material_implicit_generated_texture(type))
            throw std::invalid_argument(
                "Fragment implicit texture coordinates require an unavailable source binding");
          native_material_fragment_context_value("BTS_Coord_Generated",
                                                 true,
                                                 c.layout.paired_physical_sides,
                                                 false,
                                                 c.layout.generated_stream &&
                                                     c.layout.resident_raster_origin);
          c.diagnostic.generated_context = true;
        }
    if (type == "ShaderNodeUVMap") {
      const auto *settings = static_cast<const NodeShaderUVMap *>(node->storage);
      require(settings, "Fragment UV Map settings are missing");
      if (settings->uv_map[0] && c.layout.uv_name != settings->uv_map)
        throw NativeFragmentContextError(
            NativeFragmentContextCode::Images,
            "Fragment source requests another named UV stream; it cannot alias the NormalMap tangent UV");
    }
    if (type == "ShaderNodeObjectInfo" || type == "ShaderNodeParticleInfo")
      throw std::invalid_argument(
          "Fragment phase1 source requires an unavailable object/particle binding");
    if (type == "ShaderNodeVectorTransform") {
      const auto *transform = static_cast<const NodeShaderVectTransform *>(node->storage);
      require(transform && transform->convert_from == transform->convert_to,
              "Fragment phase1 vector transform requires unavailable object/view matrices");
    }
    if (type == "ShaderNodeNormalMap") {
      const auto *normal = static_cast<const NodeShaderNormalMap *>(node->storage);
      require(normal && (normal->space == SHD_SPACE_TANGENT || normal->space == SHD_SPACE_OBJECT ||
                         normal->space == SHD_SPACE_WORLD),
              "Unsupported Normal Map space");
      if (normal->space == SHD_SPACE_OBJECT) {
        require(c.layout.resident_object_context && c.layout.resident_raster_origin,
                "Object Normal Map requires captured object matrices");
        c.diagnostic.object_matrices_context = true;
      }
      require(
          (node->custom1 & 1) == 0,
          "Fragment Normal Map alternate derivative basis is unsupported; original custom1 is unchanged");
      require(normal->convention == SHD_NORMAL_MAP_CONVENTION_OPENGL ||
                  normal->convention == SHD_NORMAL_MAP_CONVENTION_DIRECTX,
              "Fragment Normal Map convention is unsupported");
      require(normal->base == SHD_NORMAL_MAP_BASE_ORIGINAL ||
                  normal->base == SHD_NORMAL_MAP_BASE_DISPLACED,
              "Fragment Normal Map base is unsupported");
      if (normal->space == SHD_SPACE_TANGENT)
        require(c.layout.tangent_stream && c.layout.uv_name == normal->uv_map,
                "Fragment Normal Map needs its exact named UV/CD_TANGENT stream");
      ++c.diagnostic.normal_maps;
    }
    if (type == "ShaderNodeTexImage" || type == "ShaderNodeTexEnvironment") {
      if (!c.layout.resident_images || !c.layout.resident_raster_origin)
        throw NativeFragmentContextError(
            NativeFragmentContextCode::Images,
            "Fragment image source requires an explicitly retained resident owner");
      auto images = NativeFragmentImageContext::capture(c.prepared);
      c.diagnostic.image_context = !images.bindings().empty();
      c.diagnostic.object_matrices_context |= images.requires_object_matrices();
      if (c.diagnostic.object_matrices_context &&
          (!c.layout.resident_object_context || !c.layout.resident_raster_origin))
        throw NativeFragmentContextError(
            NativeFragmentContextCode::Images,
            "BOX Image Texture requires actual captured resident owner matrices");
    }
    if (type == "ShaderNodeFresnel" || type == "ShaderNodeLayerWeight") {
      if (!c.layout.resident_view_context || !c.layout.resident_raster_origin)
        throw NativeFragmentContextError(
            NativeFragmentContextCode::View,
            "Fragment Fresnel/Layer Weight requires actual view context");
      c.diagnostic.view_context = true;
    }
    if (type == "ShaderNodeCamera" || type == "ShaderNodeLightPath")
      throw std::invalid_argument("Fragment source requires unsupported full view/ray context");
    if (type == "ShaderNodeBump") {
      const auto *width = enabled_input(*node, "Filter Width");
      require(
          width && !width->link,
          "Fragment phase1 linked Filter Width is unsupported: Blender split derivative offsets require a static literal");
      const auto *height = enabled_input(*node, "Height");
      if (height && height->link)
        ++c.diagnostic.active_bumps;
    }
  }
}
void construct(void *data, GPUMaterial *material)
{
  auto &c = *static_cast<Compilation *>(data);
  try {
    GPU_material_hosted_ramps_enable(material);
    ntreeGPUMaterialNodes(c.prepared.tree, material);
    const auto images = NativeFragmentImageContext::capture(c.prepared);
    std::size_t sampler_count = c.diagnostic.baked_ao_context ? 1 : 0;
    for (const auto &texture : GPU_material_textures(material)) {
      sampler_count += native_image_sampler_slots(bool(texture.tiled_mapping_name[0]));
      require(!texture.sky, "Fragment sky texture requires a separate owner");
      if (texture.ima) {
        require(c.layout.resident_images &&
                    std::any_of(images.bindings().begin(),
                                images.bindings().end(),
                                [&](const auto &binding) {
                                  return binding.image == texture.ima &&
                                         (binding.decoded ||
                                          (binding.tiled && (texture.tiled_mapping_name[0] ?
                                                                 binding.tiled_mapping :
                                                                 binding.first_tile_2d)));
                                }),
                "Fragment shader Image has no exact retained decoded source/representation");
        require(!texture.iuser_available || (!texture.iuser.scene && !texture.iuser.pass &&
                                             !texture.iuser.layer && !texture.iuser.multi_index &&
                                             !texture.iuser.view && !texture.iuser.multiview_eye &&
                                             (texture.ima->source != IMA_SRC_TILED ||
                                              native_tiled_default_user_tile(texture.iuser.tile))),
                "Fragment image requires default retained ImageUser");
      }
      else
        require(texture.colorband, "Fragment non-image sampler has no retained ramp owner");
    }
    require(GPU_max_textures() > 0 && sampler_count <= std::size_t(GPU_max_textures()),
            "Fragment source sampler count exceeds GPU capacity");
    // Stock Output AOV sets OBJECT_INFO unconditionally (node_shader_output_aov.cc87).
    // Our exact fragment_aov capture omits that stock object's holdout/flag path;
    // the coarse material flag is therefore not evidence of a required binding.
    const auto *uniform = GPU_material_uniform_attributes(material);
    require(!uniform || uniform->list.is_empty(),
            "Fragment phase1 uniform object/instancer attributes need a physical binding");
    const auto *layer = GPU_material_layer_attributes(material);
    require(!layer || layer->is_empty(),
            "Fragment phase1 view-layer attributes need a physical binding");
    std::string key =
        "mPainter fragment-normal v7 unbiased-image-LOD retained-project-AO real-view actual-ORCO resident-object explicit-tangent UV-pixels raw-world;" +
        c.layout.uv_name + ";N-Ni-Ng;" + std::to_string(c.count) +
        ";tangent=" + std::to_string(c.layout.tangent_stream);
    key += ";surface=" + std::string(native_world_normal_surface_name(
                             c.prepared.source->world_normal_surface_contract));
    if (c.diagnostic.object_matrices_context)
      key += ";stock-BOX-captured-model-v1";
    if (c.diagnostic.baked_ao_context)
      key += ";retained-project-AO-rasterUV-v1";
    if (c.diagnostic.view_context)
      key += ";real-view-two-float4-v1";
    if (c.layout.generated_stream)
      key += ";actual-ORCO-stream-v1";
    if (c.layout.resident_object_context)
      key += ";captured-owner-v1";
    if (c.layout.resident_raster_origin)
      key += ";resident-raster-origin-v1";
    if (c.layout.paired_physical_sides)
      key += ";physical-front-back-v1";
    key += ";fill-projection-v1=" + std::to_string(c.layout.fill_projection);
    for (const auto &a : GPU_material_attributes(material)) {
      key += ';' + std::string(a.name) + ':' + std::to_string(a.type) + ':' + attribute(a, c);
      if (std::string_view(a.name).starts_with(native_mapping_attribute_prefix))
        c.diagnostic.mapping_rows = true;
    }
    const auto input_rows = std::max<std::uint64_t>(
        1, native_mapping_parameter_rows(c.count, c.diagnostic.mapping_rows));
    require(resource_product(input_rows, sizeof(std::array<float, 4>)) <=
                GPU_max_storage_buffer_size(),
            "Fragment parameter layout exceeds SSBO limit before shader compilation");
    std::vector<std::string> literals;
    for (const auto *node : c.required)
      if (STREQ(node->idname, "ShaderNodeBump")) {
        const auto *width = enabled_input(*node, "Filter Width");
        require(width && width->default_value, "Fragment Bump Filter Width missing");
        literals.push_back(
            std::string(node->name) + ";widthbits=" +
            std::to_string(std::bit_cast<std::uint32_t>(
                static_cast<const bNodeSocketValueFloat *>(width->default_value)->value)));
      }
    std::sort(literals.begin(), literals.end());
    for (const auto &literal : literals)
      key += ";" + literal;
    for (int i = 0; i < GPU_material_hosted_ramp_count(material); ++i) {
      int parameter, row;
      GPU_material_hosted_ramp_binding(material, i, parameter, row);
      key += ";ramp=" + std::to_string(parameter) + ':' + std::to_string(row);
    }
    GPU_material_set_hosted_cache_key(material, BLI_hash_string(key.c_str()));
  }
  catch (const std::exception &e) {
    c.error = e.what();
    GPU_material_set_hosted_cache_key(material,
                                      BLI_hash_string("mPainter fragment-normal rejected v1"));
  }
}
void codegen_impl(Compilation &c, GPUMaterial *material, GPUCodegenOutput &output)
{
  using namespace gpu::shader;
  c.diagnostic.codegen_invoked = true;
  require(c.error.empty(), c.error);
  require(output.displacement.empty(),
          "Private Normal AOV unexpectedly generated displacement code");
  auto &info = *reinterpret_cast<ShaderCreateInfo *>(output.create_info);
  info.vertex_inputs_.clear();
  info.vertex_out_interfaces_.clear();
  c.varying.smooth(Type::float2_t, "fragment_uv")
      .smooth(Type::float3_t, "fragment_P")
      .smooth(Type::float3_t, "fragment_N")
      .smooth(Type::float3_t, "fragment_Ni")
      .smooth(Type::float3_t, "fragment_Ng");
  if (c.diagnostic.baked_ao_context) {
    c.varying.smooth(Type::float2_t, "fragment_raster_uv");
    int slot = 0;
    for (const auto *resources :
         {&info.batch_resources_, &info.pass_resources_, &info.geometry_resources_})
      for (const auto &resource : *resources)
        if (resource.bind_type == ShaderCreateInfo::Resource::SAMPLER)
          slot = std::max(slot, resource.slot + 1);
    require(slot < GPU_max_textures(),
            "Retained project AO exceeds the fragment sampled texture budget");
    info.sampler(slot, ImageType::Float2D, "native_project_ao", Frequency::BATCH);
  }
  info.auto_resource_location(true)
      .vertex_in(0, Type::float2_t, "raster_uv")
      .vertex_in(1, Type::float2_t, "authored_uv")
      .vertex_in(2, Type::float3_t, "world_P")
      .vertex_in(3, Type::float3_t, "world_N")
      .vertex_in(4, Type::float3_t, "world_Ni")
      .vertex_in(5, Type::float3_t, "world_Ng")
      .vertex_out(c.varying)
      .fragment_out(0, Type::float4_t, "world_normal_out")
      .storage_buf(0, Qualifier::read, "float4", "native_parameters[]");
  if (c.layout.tangent_stream) {
    c.varying.smooth(Type::float4_t, "fragment_tangent");
    info.vertex_in(6, Type::float4_t, "object_tangent");
  }
  if (c.layout.generated_stream) {
    c.varying.smooth(Type::float3_t, "fragment_generated");
    info.vertex_in(7, Type::float3_t, "original_generated");
  }
  require(info.push_constants_.is_empty(),
          "Unexpected fragment source push fields need explicit layout admission");
  const auto pushes = native_fragment_push_fields(c.layout.tangent_stream,
                                                  c.diagnostic.object_context,
                                                  c.layout.paired_physical_sides,
                                                  c.layout.resident_raster_origin,
                                                  c.diagnostic.view_context);
  require(native_fragment_push_bytes(pushes) <= 128,
          "Fragment push layout exceeds portable128-byte budget");
  for (const auto &field : pushes) {
    Type type = Type::float_t;
    switch (field.type) {
      case NativeFragmentPushType::Float:
        break;
      case NativeFragmentPushType::Float2:
        type = Type::float2_t;
        break;
      case NativeFragmentPushType::Float3:
        type = Type::float3_t;
        break;
      case NativeFragmentPushType::Float4:
        type = Type::float4_t;
        break;
      case NativeFragmentPushType::Mat3:
        type = Type::float3x3_t;
        break;
      case NativeFragmentPushType::Mat4:
        type = Type::float4x4_t;
        break;
    }
    info.push_constant(type, field.name);
  }
  if (c.diagnostic.normal_maps || c.diagnostic.object_context ||
      c.diagnostic.object_matrices_context)
    info.typedef_source("draw_shader_shared.hh");
  if (c.diagnostic.normal_maps || c.diagnostic.object_context)
    info.storage_buf(1, Qualifier::read, "ObjectInfos", "native_object_infos[]");
  if (c.diagnostic.object_matrices_context)
    info.storage_buf(2, Qualifier::read, "ObjectMatrices", "native_object_matrices[]");
  if (c.layout.fill_projection)
    info.storage_buf(3, Qualifier::read, "float4", "native_projection[]");
  // Stock eevee_attributes_mesh_lib.glsl29-33: inverse-transpose, normalize
  // per vertex, then interpolate all four components without renormalizing.
  const std::string vertex =
      "void main(){fragment_uv=authored_uv;fragment_P=world_P;" +
      std::string(c.diagnostic.baked_ao_context ? native_baked_ao_vertex_assignment() :
                                                  std::string_view{}) +
      std::string(c.layout.generated_stream ? "fragment_generated=original_generated;" : "") +
      std::string(
          c.layout.paired_physical_sides ?
              "fragment_N=world_N*native_normal_side;fragment_Ni=world_Ni*native_normal_side;fragment_Ng=world_Ng*native_normal_side;" :
              "fragment_N=world_N;fragment_Ni=world_Ni;fragment_Ng=world_Ng;") +
      std::string(
          c.layout.tangent_stream ?
              (c.diagnostic.object_context ?
                   "fragment_tangent=vec4(safe_normalize(object_tangent.xyz*mat3(native_owner_world_to_object)),object_tangent.w);" :
                   "fragment_tangent=vec4(safe_normalize(object_tangent.xyz*native_world_to_object),object_tangent.w);") :
              "") +
      std::string(c.layout.resident_raster_origin ?
                      "gl_Position=vec4((raster_uv-native_raster_origin)*2.0-1.0,0,1);}" :
                      "gl_Position=vec4(raster_uv*2.0-1.0,0,1);}");
  info.vertex_source("gpu_shader_python_vert.glsl");
  info.generated_sources.append(
      {"gpu_shader_python_vert.glsl",
       c.layout.tangent_stream ? Vector<StringRefNull>{"gpu_shader_math_vector_safe_lib.glsl"} :
                                 Vector<StringRefNull>{},
       vertex});
  std::string fields, initialize;
  int projection_domains = 0;
  for (const auto &a : GPU_material_attributes(material)) {
    const std::string_view name(a.name);
    if (name == "BTS_Coord_UV" ||
        (a.type != CD_ORCO && (name.empty() || name == c.layout.uv_name)))
      projection_domains |= 1;
    else if (name == "BTS_Geometry_Position")
      projection_domains |= 2;
    else if (name == "BTS_Coord_Object")
      projection_domains |= 4;
    else if (name == "BTS_Coord_Generated" || (name.empty() && a.type == CD_ORCO))
      projection_domains |= 8;
    fields += (a.type == CD_ORCO ? "vec3 v" : "vec4 v") + std::to_string(a.id) + ";\n";
    initialize += "var_attrs.v" + std::to_string(a.id) + "=" + attribute(a, c) + ";\n";
  }
  for (auto *resources :
       {&info.batch_resources_, &info.pass_resources_, &info.geometry_resources_})
    for (auto &resource : *resources)
      resource.info_name = "bts_fragment_normal";
  std::string types = "struct Closure {float unused;};\n#define CLOSURE_DEFAULT Closure(0.0)\n" +
                      info.typedef_source_generated;
  if (!fields.empty())
    types += "struct BtsAttributes {" + fields + "} var_attrs;\n";
  types +=
      "float derivative_scale_get(){return 1.0;}\nvec4 attr_load_color_post(vec4 v){return v;}\nfloat attr_load_temperature_post(float v){return v;}\nvec4 attr_load_uniform(vec4 v,uint hash){return v;}\n";
  // This pass rasterizes at the Texture Set resolution, with no EEVEE film scaling.
  types += "float texture_lod_bias_get(){return 1.0;}\n";
  for (const char *frequency : {"PASS", "BATCH", "GEOMETRY"}) {
    const std::string macro = std::string("CREATE_INFO_RES_") + frequency + "_bts_fragment_normal";
    types += "\n#ifdef " + macro + "\n" + macro + "\n#endif\n";
  }
  if (c.diagnostic.view_context)
    types += native_fragment_view_source();
  if (c.diagnostic.baked_ao_context)
    types += native_baked_ao_fragment_source();
  if (c.diagnostic.normal_maps || c.diagnostic.object_context)
    types += "ObjectInfos object_infos_get(){return native_object_infos[0];}\n";
  if (c.diagnostic.object_matrices_context)
    types += native_fragment_image_object_matrices_source();
  if (c.layout.fill_projection) {
    types +=
        "vec4 bts_projected_tangent;\nvec3 bts_uv,bts_position,bts_object,bts_generated,bts_normal,bts_object_normal;\nvec3 bts_real_uv,bts_real_position,bts_real_object,bts_real_generated,bts_real_normal,bts_real_object_normal;\nfloat bts_real_footprint;\n#define native_tile_offset native_raster_origin\nconst int bts_fill_domains=" +
        std::to_string(projection_domains) + ";\n" + native_material_projection_source();
  }
  // Runtime generated roots emit their real dependencies before this block.
  // typedef_source alone is static metadata, not a runtime source include.
  Vector<StringRefNull> type_dependencies;
  if (c.diagnostic.normal_maps || c.diagnostic.object_context ||
      c.diagnostic.object_matrices_context)
  {
    type_dependencies.append("draw_shader_shared.hh");
    type_dependencies.append("gpu_shader_utildefines_lib.glsl");
  }
  info.generated_sources.append({"gpu_shader_python_typedef_lib.glsl", type_dependencies, types});
  Vector<StringRefNull> dependencies = {"gpu_shader_python_typedef_lib.glsl",
                                        "gpu_shader_codegen_lib.glsl"};
  std::string functions;
  c.diagnostic.material_functions = int(output.material_functions.size());
  for (const auto &function : output.material_functions)
    functions += function.serialized;
  const auto transform_calls = functions + output.surface.serialized;
  for (const auto &function : output.material_functions)
    for (const auto &dependency : function.dependencies)
      if (native_fragment_image_keep_dependency(
              dependency, c.diagnostic.object_matrices_context, transform_calls))
        dependencies.append(dependency);
  auto body = output.surface.serialized;
  const std::string from = "node_output_aov(", to = "fragment_aov(";
  for (std::size_t p = 0; (p = body.find(from, p)) != std::string::npos; p += to.size())
    body.replace(p, from.size(), to);
  for (const auto &dependency : output.surface.dependencies)
    if (dependency != "gpu_shader_material_output_aov.glsl" &&
        native_fragment_image_keep_dependency(
            dependency, c.diagnostic.object_matrices_context, transform_calls))
      dependencies.append(dependency);
  if (c.diagnostic.active_bumps) {
    require(c.diagnostic.material_functions > 0 && body.find("node_bump(") != std::string::npos &&
                body.find("dF_branch_incomplete(") != std::string::npos,
            "Prepared active Bump lost its real split fragment derivative path");
  }
  // Differentiate physical position in target raster UV, not the source NormalMap UV.
  // This removes mesh curvature/orientation while retaining the complete native chain.
  const std::string tangent_output =
      c.layout.tangent_output ?
          "vec3 N=normalize(fragment_N),du=dFdx(fragment_P),dv=dFdy(fragment_P);vec3 T=normalize(du-N*dot(N,du));vec3 B=normalize(cross(N,T));if(dot(B,dv)<0.0)B=-B;vec3 W=normalize(fragment_result);fragment_result=vec3(dot(W,T),dot(W,B),dot(W,N))*.5+.5;" :
          "";
  std::string projected_tangent;
  if (c.layout.fill_projection && c.diagnostic.normal_maps)
    projected_tangent = R"GLSL(
vec4 bts_fill_tangent(){
 if(!bts_fill_enabled()||(bts_fill_projection_mode==0&&native_projection[0].z!=0)||bts_fill_projection_mode==6)return fragment_tangent;
 vec3 px=dFdx(fragment_P),py=dFdy(fragment_P),N=normalize(fragment_N);
 vec2 ux=dFdx(bts_uv.xy),uy=dFdy(bts_uv.xy);float det=ux.x*uy.y-ux.y*uy.x;
 if(abs(det)<=1e-20)return fragment_tangent;
 vec3 raw_T=(px*uy.y-py*ux.y)/det,raw_B=(py*ux.x-px*uy.x)/det,T=raw_T-N*dot(raw_T,N);
 if(dot(T,T)<=1e-20)return fragment_tangent;
 T=normalize(T);float hand=dot(cross(N,T),raw_B)<0?-1 : 1;
 float facing=gl_FrontFacing?1:-1,negative=(object_infos_get().flag&OBJECT_NEGATIVE_SCALE)!=0?-1 : 1;
 return vec4(T*facing,hand*negative*facing);
}
)GLSL";
  std::string evaluate =
      initialize +
      "fragment_result=vec3(0);fragment_has_normal=false;fragment_evaluate();float coverage=fragment_has_normal?1.0:0.0;";
  if (c.layout.fill_projection) {
    evaluate =
        "bts_real_uv=vec3(fragment_uv,0);bts_real_position=fragment_P;bts_real_normal=normalize(fragment_N);bts_real_object_normal=bts_real_normal;";
    evaluate += "bts_real_object=" +
                std::string(c.diagnostic.object_context ?
                                "(native_owner_world_to_object*vec4(fragment_P,1)).xyz" :
                                "vec3(0)") +
                ";bts_real_generated=" +
                std::string(c.layout.generated_stream ? "fragment_generated" : "vec3(0)") +
                ";bts_real_footprint=max(length(dFdx(fragment_P)),length(dFdy(fragment_P)));";
    evaluate +=
        "bool covered=bts_fill_prepare();int mode=bts_fill_enabled()?bts_fill_projection_mode:0;int axes=mode==1?3:1,taps=mode>0&&mode<6&&bts_fill_source_filter==0?4:1;vec3 accumulated=vec3(0);float coverage=0;for(int axis=0;axis<axes;++axis)for(int tap=0;tap<taps;++tap){float weight;bool selected=bts_fill_select(axis,tap,weight);" +
        std::string(c.diagnostic.normal_maps ? "bts_projected_tangent=bts_fill_tangent();" : "") +
        initialize +
        "fragment_result=vec3(0);fragment_has_normal=false;fragment_evaluate();if(covered&&selected&&fragment_has_normal){accumulated+=fragment_result*weight;coverage+=weight;}}fragment_result=coverage>0?accumulated/coverage:bts_real_normal;coverage=clamp(coverage,0.0,1.0);";
  }
  const std::string fragment =
      projected_tangent + functions +
      "\nvec3 fragment_result;bool fragment_has_normal;\nvoid fragment_aov(vec4 color,float value,float hash,out Closure dummy){dummy=CLOSURE_DEFAULT;if(floatBitsToUint(hash)==" +
      std::to_string(BLI_hash_string("BTS_Channel")) +
      "u){fragment_result=color.rgb;fragment_has_normal=true;}}\nClosure fragment_evaluate(){\n" +
      body +
      "\n}\nvoid main(){g_data.P=fragment_P;g_data.N=normalize(fragment_N);g_data.Ni=fragment_Ni;g_data.Ng=normalize(fragment_Ng);\n" +
      evaluate + tangent_output + "world_normal_out=vec4(fragment_result,coverage);}\n";
  info.fragment_source("gpu_shader_python_frag.glsl");
  info.generated_sources.append({"gpu_shader_python_frag.glsl", dependencies, fragment});
  c.diagnostic.generated = types + vertex + fragment;
}
void codegen(void *data, GPUMaterial *material, GPUCodegenOutput *output)
{
  auto &c = *static_cast<Compilation *>(data);
  try {
    codegen_impl(c, material, *output);
  }
  catch (const std::exception &e) {
    c.error = e.what();
    using namespace gpu::shader;
    auto &info = *reinterpret_cast<ShaderCreateInfo *>(output->create_info);
    ShaderCreateInfo failed("Rejected native fragment Normal");
    failed.vertex_source("gpu_shader_python_vert.glsl");
    failed.generated_sources.append(
        {"gpu_shader_python_vert.glsl", {}, "void main(){gl_Position=vec4(0);}"});
    failed.fragment_out(0, Type::float4_t, "rejected")
        .fragment_source("gpu_shader_python_frag.glsl");
    failed.generated_sources.append(
        {"gpu_shader_python_frag.glsl", {}, "void main(){rejected=vec4(0);}"});
    info = std::move(failed);
  }
}
struct State {
  gpu::FrameBuffer *fb = GPU_framebuffer_active_get();
  GPUBlend blend = GPU_blend_get();
  GPUDepthTest depth = GPU_depth_test_get();
  GPUFaceCullTest cull = GPU_face_culling_get();
  GPUStencilTest stencil = GPU_stencil_test_get();
  GPUWriteMask write = GPU_write_mask_get();
  int viewport[4], scissor[4];
  GLint front = 0;
  bool scissor_enabled = false, srgb = false;
  State()
  {
    GPU_apply_state();
    scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    GPU_viewport_size_get_i(viewport);
    GPU_scissor_get(scissor);
    glGetIntegerv(GL_FRONT_FACE, &front);
  }
  ~State()
  {
    if (fb)
      GPU_framebuffer_bind(fb);
    else
      GPU_framebuffer_restore();
    GPU_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    GPU_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    GPU_scissor_test(scissor_enabled);
    GPU_front_facing(front == GL_CW);
    GPU_blend(blend);
    GPU_depth_test(depth);
    GPU_face_culling(cull);
    GPU_stencil_test(stencil);
    GPU_write_mask(write);
    if (srgb)
      glEnable(GL_FRAMEBUFFER_SRGB);
    else
      glDisable(GL_FRAMEBUFFER_SRGB);
  }
};
}  // namespace
NativeFragmentDiagnostics inspect_native_fragment_source_context(
    const PreparedNativeFragmentNormal &prepared, const NativeFragmentLayout &layout)
{
  Compilation c;
  c.prepared = prepared;
  c.layout = layout;
  inspect(c);
  return c.diagnostic;
}
namespace {
blender::ImageGPUTextures retained_fragment_texture(const blender::GPUMaterialTexture &binding,
                                                    const NativeFragmentImageContext &images)
{
  using namespace blender;
  if (!binding.ima)
    throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                     "Fragment image binding has no retained Image");
  if (binding.iuser_available &&
      (binding.iuser.scene || binding.iuser.pass || binding.iuser.layer ||
       binding.iuser.multi_index || binding.iuser.view || binding.iuser.multiview_eye ||
       (binding.ima->source == IMA_SRC_TILED &&
        !native_tiled_default_user_tile(binding.iuser.tile))))
    throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                     "Fragment image sampler requires default ImageUser");
  if (binding.ima->source == IMA_SRC_TILED) {
    const bool array = bool(binding.tiled_mapping_name[0]);
    if (std::none_of(images.bindings().begin(), images.bindings().end(), [&](const auto &owned) {
          return owned.image == binding.ima && owned.tiled &&
                 (array ? owned.tiled_mapping : owned.first_tile_2d);
        }))
      throw NativeFragmentContextError(
          NativeFragmentContextCode::Images,
          "Fragment TILED representation has no exact retained inventory");
    return retained_native_tiled_gpu_image(images.source(),
                                           binding.ima,
                                           array ? NativeTiledRepresentation::ArrayMapping :
                                                   NativeTiledRepresentation::FirstTile2D);
  }
  if (binding.tiled_mapping_name[0])
    throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                     "Fragment FILE image unexpectedly requires tile mapping");
  auto cached = BKE_image_gpu_override_texture(binding.ima);
  if (!cached.texture || !*cached.texture)
    throw NativeFragmentImagePending(
        "Native fragment image upload is pending; target is unchanged");
  if ((binding.sampler_state.filtering & GPU_SAMPLER_FILTERING_MIPMAP) &&
      !BKE_image_gpu_mipmaps_suspended(binding.ima) &&
      !(binding.ima->runtime->gpuflag & IMA_GPU_MIPMAP_COMPLETE))
    throw NativeFragmentImagePending(
        "Native fragment image mip upload is pending; target is unchanged");
  auto *texture = *cached.texture;
  const auto format = GPU_texture_format(texture);
  const auto precision = format == gpu::TextureFormat::SFLOAT_32_32_32_32 ?
                             NativeFragmentImagePrecision::Float32Rgba :
                         format == gpu::TextureFormat::SFLOAT_16_16_16_16 ?
                             NativeFragmentImagePrecision::Float16Rgba :
                             NativeFragmentImagePrecision::Unsupported;
  images.validate_storage(binding.ima,
                          GPU_texture_width(texture),
                          GPU_texture_height(texture),
                          GPU_texture_dimensions(texture) == 2 && !GPU_texture_is_array(texture),
                          precision);
  return cached;
}
void validate_retained_fragment_textures(blender::GPUMaterial *material,
                                         const NativeFragmentImageContext &images)
{
  for (const auto &binding : blender::GPU_material_textures(material))
    if (binding.ima)
      retained_fragment_texture(binding, images);
}
void bind_retained_fragment_textures(blender::GPUMaterial *material,
                                     blender::gpu::Shader *shader,
                                     const NativeFragmentImageContext &images)
{
  using namespace blender;
  for (const auto &binding : GPU_material_textures(material))
    if (binding.ima) {
      const auto cached = retained_fragment_texture(binding, images);
      auto state = native_material_image_sampler(
          binding.sampler_state,
          GPU_anisotropic_filtering_flags(U.anisotropic_filter),
          BKE_image_gpu_mipmaps_suspended(binding.ima));
      GPU_texture_bind_ex(
          *cached.texture, state, GPU_shader_get_sampler_binding(shader, binding.sampler_name));
      if (binding.tiled_mapping_name[0]) {
        if (!cached.tile_mapping || !*cached.tile_mapping)
          throw NativeFragmentImagePending(
              "Native TILED mapping upload is pending; target is unchanged");
        GPU_texture_bind_ex(*cached.tile_mapping,
                            state,
                            GPU_shader_get_sampler_binding(shader, binding.tiled_mapping_name));
      }
    }
    else {
      require(binding.colorband && *binding.colorband, "Fragment ramp texture missing");
      GPU_texture_bind(*binding.colorband,
                       GPU_shader_get_sampler_binding(shader, binding.sampler_name));
    }
}
}  // namespace
NativeFragmentLayout native_world_normal_source_request(
    const PreparedNativeFragmentNormal &prepared)
{
  require(prepared.tree, "Native world Normal source tree is missing");
  Compilation c;
  c.prepared = prepared;
  c.layout.resident_baked_ao_context = true;
  c.layout.resident_view_context = true;
  c.layout.generated_stream = true;
  c.layout.tangent_stream = true;
  c.layout.resident_raster_origin = true;
  c.layout.resident_object_context = true;
  c.layout.paired_physical_sides = true;
  c.layout.resident_images = true;
  std::optional<std::string> uv;
  std::set<const bNode *> visited;
  std::vector<const bNode *> pending;
  for (const auto &node : prepared.tree->nodes)
    if (STREQ(node.idname, "ShaderNodeOutputAOV"))
      pending.push_back(&node);
  while (!pending.empty()) {
    const auto *node = pending.back();
    pending.pop_back();
    if (!visited.insert(node).second)
      continue;
    for (const auto &link : prepared.tree->links)
      if (link.tonode == node)
        pending.push_back(link.fromnode);
    if (STREQ(node->idname, "ShaderNodeNormalMap")) {
      const auto *settings = static_cast<const NodeShaderNormalMap *>(node->storage);
      require(settings, "Native Normal Map settings are missing");
      if (settings->space != SHD_SPACE_TANGENT)
        continue;
      const std::string name = settings->uv_map;
      require(!uv || *uv == name,
              "Native world Normal currently requires one shared source tangent UV");
      uv = name;
    }
  }
  c.layout.uv_name = uv.value_or("");
  c.layout.default_image_uv = NativeFragmentImageContext::capture(prepared).requires_default_uv();
  inspect(c);
  c.layout.generated_stream = c.diagnostic.generated_context;
  c.layout.resident_view_context = c.diagnostic.view_context;
  c.layout.resident_baked_ao_context = c.diagnostic.baked_ao_context;
  return c.layout;
}
NativeFragmentLayout native_world_normal_source_layout(
    const PreparedNativeFragmentNormal &prepared,
    const NativeFragmentGeometry *supplied,
    const NativeMaterialViewContext *view,
    const NativeMaterialBakedAoContext *ao)
{
  auto layout = native_world_normal_source_request(prepared);
  if (layout.default_image_uv) {
    if (!supplied && !layout.uv_name.empty())
      throw NativeFragmentContextError(
          NativeFragmentContextCode::Images,
          "Native default image UV is pending actual captured stream proof");
    if (supplied)
      NativeFragmentImageContext::capture(prepared).validate_uv(*supplied, layout.uv_name);
  }
  if (layout.generated_stream) {
    if (!supplied)
      throw NativeFragmentContextError(
          NativeFragmentContextCode::GeneratedOrco,
          "Fragment Generated coordinates require captured authoritative ORCO: BTS_Coord_Generated");
    validate_native_fragment_generated_context(*supplied);
  }
  if (layout.resident_baked_ao_context) {
    if (!ao)
      native_baked_ao_reject(1001, "immutable requested AO snapshot is pending");
    ao->validate();
  }
  if (layout.resident_view_context) {
    if (!view)
      throw NativeFragmentContextError(
          NativeFragmentContextCode::View,
          "Native fragment requires a real validated view: camera is pending");
    view->validate();
  }
  return layout;
}
blender::ObjectMatrices native_fragment_image_object_matrices(
    const NativeFragmentGeometryObject &object, const blender::ObjectInfos &infos)
{
  using namespace blender;
  const auto image_reject = [](const std::string &reason) {
    throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                     "Native fragment image binding: " + reason);
  };
  validate_native_fragment_object_context(object, infos);
  ObjectMatrices result{};
  const float4x4 model(object.object_to_world.data()), inverse(object.world_to_object.data());
  bool invertible = false;
  (void)math::invert(model, invertible);
  if (!invertible)
    image_reject("BOX owner model matrix is singular");
  (void)math::invert(inverse, invertible);
  if (!invertible)
    image_reject("BOX owner inverse matrix is singular");
  // The capture authority copied both matrices from this actual owner. Preserve
  // those exact values; do not invert/rebuild the model from the inverse here.
  result.model = model;
  result.model_inverse = inverse;
  static_assert(sizeof(ObjectMatrices) == 128);
  return result;
}
NativeFragmentFacingGeometry prepare_native_fragment_facing(
    std::span<const NativeFragmentVertex> vertices, std::span<const unsigned char> front)
{
  require(vertices.size() % 3 == 0 && front.size() == vertices.size() / 3,
          "Fragment physical-facing metadata size differs");
  require(vertices.size() <= std::numeric_limits<int>::max(),
          "Fragment physical-facing vertex count exceeds batch range");
  const auto group_index = [&](std::size_t triangle) {
    require(front[triangle] <= 1, "Fragment physical-facing flag must be0/1");
    const auto *v = vertices.data() + 3 * triangle;
    const double area = (double(v[1].raster_uv[0]) - v[0].raster_uv[0]) *
                            (double(v[2].raster_uv[1]) - v[0].raster_uv[1]) -
                        (double(v[1].raster_uv[1]) - v[0].raster_uv[1]) *
                            (double(v[2].raster_uv[0]) - v[0].raster_uv[0]);
    if (!std::isfinite(area) || area == 0)
      return -1;
    return (area > 0 ? 2 : 0) + front[triangle];
  };
  std::array<std::size_t, 4> counts{}, next{};
  for (std::size_t i = 0; i < front.size(); ++i)
    if (const int group = group_index(i); group >= 0)
      counts[group] += 3;
  const auto bytes = resource_sum(resource_product(vertices.size(), sizeof(NativeFragmentVertex)),
                                  4 * sizeof(NativeFragmentFacingBatch));
  NativeFragmentFacingGeometry result;
  result.vertices.resize(counts[0] + counts[1] + counts[2] + counts[3]);
  result.batches.reserve(4);
  std::size_t first = 0;
  for (int i = 0; i < 4; ++i) {
    next[i] = first;
    if (counts[i])
      result.batches.push_back(
          {unsigned(first), unsigned(counts[i]), bool((i >= 2) != (i % 2 != 0))});
    first += counts[i];
  }
  // Two passes and one vertex allocation: stable within each of <=4 groups.
  // The original mesh is caller-owned and immutable throughout preparation.
  for (std::size_t i = 0; i < front.size(); ++i) {
    const int group = group_index(i);
    if (group < 0)
      continue;
    auto &target = next[group];
    std::copy_n(vertices.data() + 3 * i, 3, result.vertices.data() + target);
    target += 3;
  }
  return result;
}
struct NativeMaterialFragmentGpuGeometry::Impl {
  using Vbo = std::unique_ptr<blender::gpu::VertBuf, decltype(&blender::GPU_vertbuf_discard)>;
  using Batch = std::unique_ptr<blender::gpu::Batch, decltype(&blender::GPU_batch_discard)>;
  using Ibo = std::unique_ptr<blender::gpu::IndexBuf, decltype(&blender::GPU_indexbuf_discard)>;
  using Object =
      std::unique_ptr<blender::gpu::StorageBuf, decltype(&blender::GPU_storagebuf_free)>;
  struct Stream {
    Vbo vertices{nullptr, blender::GPU_vertbuf_discard},
        generated{nullptr, blender::GPU_vertbuf_discard};
    Batch batch{nullptr, blender::GPU_batch_discard};
  };
  struct Tile {
    int number = 1001;
    std::uint32_t first = 0, count = 0;
  };
  std::shared_ptr<const NativeFragmentGeometry> source;
  std::thread::id thread = std::this_thread::get_id();
  void *context = blender::GPU_context_active_get();
  Ibo indices{nullptr, blender::GPU_indexbuf_discard};
  std::vector<Stream> streams;
  std::vector<Object> objects;
  std::vector<bool> object_uploaded;
  NativeFragmentImageOwnerCache<std::shared_ptr<blender::gpu::StorageBuf>> object_matrices;
  std::vector<std::uint32_t> range_order;
  std::vector<Tile> tiles;
  NativeFragmentResidentDiagnostics diagnostic;
  void owner() const
  {
    if (std::this_thread::get_id() != thread || !context ||
        blender::GPU_context_active_get() != context)
      throw std::logic_error("Resident fragment geometry requires its original current GPU owner");
  }
  ~Impl()
  {
    if (indices || !streams.empty() || !objects.empty() || !object_matrices.empty()) {
      try {
        owner();
      }
      catch (...) {
        std::fprintf(stderr, "Resident fragment geometry retired outside its GPU owner\n");
        std::terminate();
      }
      streams.clear();
      indices.reset();
      objects.clear();
      object_matrices.clear();
    }
  }
};
NativeMaterialFragmentGpuGeometry::NativeMaterialFragmentGpuGeometry(
    std::shared_ptr<const NativeFragmentGeometry> source)
    : impl_(std::make_unique<Impl>())
{
  using namespace blender;
  auto &s = *impl_;
  s.owner();
  require(bool(source), "Resident fragment geometry source missing");
  const auto streams = source->streams();
  const auto objects = source->objects();
  const auto ranges = source->ranges();
  const auto indices = source->indices();
  require(!streams.empty() && !objects.empty() && !ranges.empty() && !indices.empty() &&
              indices.size() % 3 == 0 && indices.size() <= std::numeric_limits<int>::max() &&
              ranges.size() <= std::numeric_limits<std::uint32_t>::max(),
          "Resident fragment index/range count outside draw API");
  std::uint64_t vertex_count = 0,
                gpu_bytes = resource_sum(resource_product(indices.size(), sizeof(std::uint32_t)),
                                         resource_product(objects.size(), sizeof(ObjectInfos)));
  int index_max = 0;
  for (std::size_t i = 0; i < streams.size(); ++i) {
    const auto vertices = source->vertices(i);
    require(streams[i].object < objects.size() && !vertices.empty() &&
                vertices.size() <= std::numeric_limits<int>::max(),
            "Resident fragment stream owner/vertex count invalid");
    vertex_count = resource_sum(vertex_count, vertices.size());
    index_max = std::max(index_max, int(vertices.size() - 1));
    for (const auto &v : vertices) {
      for (const auto *uv : {&v.raster_uv, &v.uv})
        for (float x : *uv)
          require(std::isfinite(x), "Nonfinite resident fragment UV");
      for (const auto *n : {&v.position, &v.normal, &v.initial_normal, &v.geometric_normal})
        for (float x : *n)
          require(std::isfinite(x), "Nonfinite resident fragment surface");
      for (float x : v.tangent)
        require(std::isfinite(x), "Nonfinite resident fragment tangent");
      require(v.tangent[3] == 1 || v.tangent[3] == -1,
              "Resident fragment tangent handedness must be +/-1");
    }
  }
  for (const auto &r : ranges) {
    require(r.stream < streams.size() && r.index_count && r.index_count % 3 == 0 &&
                std::uint64_t(r.first_index) + r.index_count <= indices.size() &&
                r.first_index <= std::numeric_limits<int>::max() &&
                r.index_count <= std::numeric_limits<int>::max(),
            "Resident fragment range outside index draw API");
    for (float x : r.raster_origin)
      require(std::isfinite(x), "Resident fragment raster origin is nonfinite");
    for (std::uint64_t i = r.first_index; i < std::uint64_t(r.first_index) + r.index_count; ++i)
      require(indices[i] < source->vertices(r.stream).size(),
              "Resident fragment index references another stream");
  }
  if (source->generated_requested()) {
    validate_native_fragment_generated_context(*source);
    for (std::size_t i = 0; i < streams.size(); ++i)
      gpu_bytes = resource_sum(
          gpu_bytes, resource_product(source->generated(i).size(), sizeof(std::array<float, 3>)));
  }
  for (std::size_t i = 0; i < objects.size(); ++i)
    validate_native_fragment_object_context(objects[i], source->object_infos(i));
  gpu_bytes = resource_sum(gpu_bytes,
                           resource_product(vertex_count, sizeof(NativeFragmentVertex)));
  const auto metadata = resource_sum(
      sizeof(Impl),
      resource_sum(
          resource_product(streams.size(), sizeof(Impl::Stream)),
          resource_sum(
              resource_product(objects.size(), sizeof(Impl::Object) + sizeof(bool)),
              resource_product(ranges.size(), sizeof(std::uint32_t) + sizeof(Impl::Tile)))));
  // Existing immutable CPU geometry is already resident, so admit additional
  // staging/metadata only. IBO's documented no-copy optimization is not implemented.
  const auto staging = resource_sum(gpu_bytes, metadata);
  State state;
  GLint held_vao = 0, held_array = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &held_vao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &held_array);
  GLuint upload_vao = 0;
  glGenVertexArrays(1, &upload_vao);
  require(upload_vao != 0, "Resident upload VAO allocation failed");
  glBindVertexArray(upload_vao);
  const auto restore_upload = qScopeGuard([&] {
    glBindVertexArray(held_vao);
    glBindBuffer(GL_ARRAY_BUFFER, held_array);
    glDeleteVertexArrays(1, &upload_vao);
  });
  s.source = std::move(source);
  s.streams.resize(streams.size());
  s.objects.reserve(objects.size());
  s.object_uploaded.resize(objects.size(), false);
  s.range_order.resize(ranges.size());
  s.tiles.reserve(ranges.size());
  // Blender's data_len is a primitive count: it multiplies by three for TRIS.
  // Passing the index count read three times beyond the retained source span.
  s.indices.reset(GPU_indexbuf_build_from_memory(
      GPU_PRIM_TRIS, indices.data(), int(indices.size() / 3), 0, index_max, false));
  require(bool(s.indices), "Resident fragment index allocation failed");
  require(s.indices->index_len_get() == indices.size(),
          "Resident fragment GPU index count differs from source");
  GPU_indexbuf_use(s.indices.get());
  ++s.diagnostic.index_uploads;
  GPUVertFormat format{};
  const auto raster = GPU_vertformat_attr_add(
                 &format, "raster_uv", gpu::VertAttrType::SFLOAT_32_32),
             uv = GPU_vertformat_attr_add(&format, "authored_uv", gpu::VertAttrType::SFLOAT_32_32);
  std::array<unsigned int, 4> ids{};
  int k = 0;
  for (const char *name : {"world_P", "world_N", "world_Ni", "world_Ng"})
    ids[k++] = GPU_vertformat_attr_add(&format, name, gpu::VertAttrType::SFLOAT_32_32_32);
  const auto tangent = GPU_vertformat_attr_add(
      &format, "object_tangent", gpu::VertAttrType::SFLOAT_32_32_32_32);
  for (std::size_t i = 0; i < streams.size(); ++i) {
    auto &out = s.streams[i];
    const auto vertices = s.source->vertices(i);
    out.vertices.reset(GPU_vertbuf_create_with_format(format));
    require(bool(out.vertices), "Resident fragment vertex allocation failed");
    GPU_vertbuf_data_alloc(*out.vertices, vertices.size());
    GPU_vertbuf_attr_fill_stride(
        out.vertices.get(), raster, sizeof(NativeFragmentVertex), vertices[0].raster_uv.data());
    GPU_vertbuf_attr_fill_stride(
        out.vertices.get(), uv, sizeof(NativeFragmentVertex), vertices[0].uv.data());
    for (int c = 0; c < 4; ++c) {
      const auto *data = c == 0 ? vertices[0].position.data() :
                         c == 1 ? vertices[0].normal.data() :
                         c == 2 ? vertices[0].initial_normal.data() :
                                  vertices[0].geometric_normal.data();
      GPU_vertbuf_attr_fill_stride(out.vertices.get(), ids[c], sizeof(NativeFragmentVertex), data);
    }
    GPU_vertbuf_attr_fill_stride(
        out.vertices.get(), tangent, sizeof(NativeFragmentVertex), vertices[0].tangent.data());
    GPU_vertbuf_use(out.vertices.get());
    ++s.diagnostic.vertex_uploads;
    out.batch.reset(
        GPU_batch_create_ex(GPU_PRIM_TRIS, out.vertices.get(), s.indices.get(), GPUBatchFlag(0)));
    require(bool(out.batch), "Resident fragment indexed batch allocation failed");
  }
  if (s.source->generated_requested())
    for (std::size_t i = 0; i < streams.size(); ++i) {
      auto &out = s.streams[i];
      const auto values = s.source->generated(i);
      GPUVertFormat generated_format{};
      const auto field = GPU_vertformat_attr_add(
          &generated_format, "original_generated", gpu::VertAttrType::SFLOAT_32_32_32);
      out.generated.reset(GPU_vertbuf_create_with_format(generated_format));
      require(bool(out.generated), "Resident Generated allocation failed");
      GPU_vertbuf_data_alloc(*out.generated, values.size());
      GPU_vertbuf_attr_fill_stride(
          out.generated.get(), field, sizeof(values[0]), values[0].data());
      GPU_vertbuf_use(out.generated.get());
      GPU_batch_vertbuf_add(out.batch.get(), out.generated.get(), false);
      ++s.diagnostic.vertex_uploads;
    }
  for (std::size_t i = 0; i < objects.size(); ++i) {
    s.objects.emplace_back(GPU_storagebuf_create_ex(sizeof(ObjectInfos),
                                                    &s.source->object_infos(i),
                                                    GPU_USAGE_STATIC,
                                                    "Native fragment resident actual ObjectInfos"),
                           GPU_storagebuf_free);
    require(bool(s.objects.back()), "Resident fragment ObjectInfos allocation failed");
  }
  std::iota(s.range_order.begin(), s.range_order.end(), 0);
  std::sort(s.range_order.begin(), s.range_order.end(), [&](auto a, auto b) {
    return ranges[a].tile == ranges[b].tile ? a < b : ranges[a].tile < ranges[b].tile;
  });
  for (std::size_t i = 0; i < s.range_order.size(); ++i) {
    const auto tile = ranges[s.range_order[i]].tile;
    if (s.tiles.empty() || s.tiles.back().number != tile)
      s.tiles.push_back({tile, std::uint32_t(i), 0});
    ++s.tiles.back().count;
  }
  s.diagnostic.revision = s.source->revision();
  s.diagnostic.storage_bytes = gpu_bytes;
  s.diagnostic.objects = objects.size();
  s.diagnostic.streams = streams.size();
  s.diagnostic.vertices = vertex_count;
  s.diagnostic.indices = s.indices->index_len_get();
  s.diagnostic.ranges = ranges.size();
}
NativeMaterialFragmentGpuGeometry::~NativeMaterialFragmentGpuGeometry() = default;
const NativeFragmentGeometry &NativeMaterialFragmentGpuGeometry::source() const
{
  impl_->owner();
  return *impl_->source;
}
const NativeFragmentResidentDiagnostics &NativeMaterialFragmentGpuGeometry::diagnostics() const
{
  impl_->owner();
  return impl_->diagnostic;
}
struct NativeMaterialFragmentGpuProgram::Impl {
  Compilation compilation;
  NativeFragmentImageContext images;
  std::weak_ptr<const NativeFragmentGeometry> image_geometry;
  std::uint64_t image_geometry_revision = 0;
  std::thread::id thread = std::this_thread::get_id();
  void *context = blender::GPU_context_active_get();
  blender::GPUMaterial *material = nullptr;
  blender::gpu::StorageBuf *parameters = nullptr, *object_infos = nullptr, *projection = nullptr;
  NativeMaterialProjection::Rows projection_rows{};
  std::vector<std::array<float, 4>> values;
  std::map<int, QJsonObject> ramps;
  void owner() const
  {
    if (std::this_thread::get_id() != thread || !context ||
        blender::GPU_context_active_get() != context)
      throw std::logic_error("Fragment program requires its original current GPU owner");
  }
  ~Impl()
  {
    if (material || parameters || object_infos || projection) {
      try {
        owner();
      }
      catch (...) {
        std::fprintf(stderr, "Native fragment program retired outside its GPU owner\n");
        std::terminate();
      }
      if (projection)
        blender::GPU_storagebuf_free(projection);
      if (parameters)
        blender::GPU_storagebuf_free(parameters);
      if (object_infos)
        blender::GPU_storagebuf_free(object_infos);
      if (material)
        blender::GPU_material_free_single(material);
    }
  }
};
NativeMaterialFragmentGpuProgram::NativeMaterialFragmentGpuProgram(
    PreparedNativeFragmentNormal prepared, NativeFragmentLayout layout)
    : impl_(std::make_unique<Impl>())
{
  using namespace blender;
  auto &s = *impl_;
  s.compilation.prepared = std::move(prepared);
  s.compilation.layout = std::move(layout);
  inspect(s.compilation);
  s.images = NativeFragmentImageContext::capture(s.compilation.prepared);
  s.owner();
  s.material = GPU_material_from_callbacks(GPU_MAT_EEVEE, construct, codegen, &s.compilation);
  require(s.compilation.error.empty(), s.compilation.error);
  require(s.material && GPU_material_status(s.material) == GPU_MAT_SUCCESS,
          "Native fragment Normal shader compilation failed");
  const auto rows = std::max<std::uint64_t>(
                 1,
                 native_mapping_parameter_rows(s.compilation.count,
                                               s.compilation.diagnostic.mapping_rows)),
             bytes = resource_product(rows, sizeof(s.values[0]));
  require(bytes <= GPU_max_storage_buffer_size(), "Fragment parameter rows exceed SSBO limit");
  s.values.resize(rows);
  s.parameters = GPU_storagebuf_create_ex(
      bytes, s.values.data(), GPU_USAGE_DYNAMIC, "Native fragment parameters");
  require(s.parameters, "Cannot allocate fragment parameters");
  if (s.compilation.layout.fill_projection) {
    s.projection = GPU_storagebuf_create_ex(sizeof(s.projection_rows),
                                            s.projection_rows.data(),
                                            GPU_USAGE_DYNAMIC,
                                            "Native fragment Fill projection");
    require(s.projection, "Cannot allocate fragment Fill projection");
  }
  if (s.compilation.diagnostic.normal_maps && !s.compilation.layout.resident_raster_origin) {
    ObjectInfos empty{};
    s.object_infos = GPU_storagebuf_create_ex(
        sizeof(empty), &empty, GPU_USAGE_DYNAMIC, "Native fragment actual ObjectInfos");
    require(s.object_infos, "Cannot allocate fragment ObjectInfos");
  }
}
NativeMaterialFragmentGpuProgram::~NativeMaterialFragmentGpuProgram() = default;
void NativeMaterialFragmentGpuProgram::validate_images() const
{
  impl_->owner();
  validate_retained_fragment_textures(impl_->material, impl_->images);
}
const NativeFragmentDiagnostics &NativeMaterialFragmentGpuProgram::diagnostics() const
{
  impl_->owner();
  return impl_->compilation.diagnostic;
}
void NativeMaterialFragmentGpuProgram::update_ramps(const NativeMaterialDescription &description)
{
  using namespace blender;
  auto &s = *impl_;
  s.owner();
  require(description.parameters.size() ==
              s.compilation.prepared.source->description.parameters.size(),
          "Fragment ramp source parameters differ");
  for (int i = 0; i < GPU_material_hosted_ramp_count(s.material); ++i) {
    int parameter, row;
    GPU_material_hosted_ramp_binding(s.material, i, parameter, row);
    require(parameter >= 0 && std::size_t(parameter) < description.parameters.size(),
            "Fragment ramp parameter missing");
    require(description.parameters[parameter].identifier ==
                s.compilation.prepared.source->description.parameters[parameter].identifier,
            "Fragment ramp source identity changed");
    require(native_material_ramp_compatible(
                description.parameters[parameter],
                s.compilation.prepared.source->description.parameters[parameter]),
            "Fragment ramp static sampling mode changed");
    const auto &value = description.parameters[parameter].ramp;
    if (s.ramps.contains(row) && s.ramps.at(row) == value)
      continue;
    const auto band = native_material_ramp(value);
    float *table = nullptr;
    int size = 0;
    BKE_colorband_evaluate_table_rgba(&band, &table, &size);
    const auto release = qScopeGuard([&] { MEM_delete(table); });
    auto *texture = GPU_material_hosted_ramp_texture(s.material);
    require(texture && size == GPU_texture_width(texture),
            "Fragment ramp lookup dimensions differ");
    GPU_texture_update_sub(texture, GPU_DATA_FLOAT, table, 0, row, 0, size, 1, 1);
    s.ramps[row] = value;
  }
}
void NativeMaterialFragmentGpuProgram::draw(blender::gpu::Texture *target,
                                            std::span<const NativeFragmentVertex> source,
                                            std::span<const std::array<float, 4>> values,
                                            std::span<const NativeMaterialMapping> mappings,
                                            const NativeFragmentPhysicalContext *physical)
{
  using namespace blender;
  auto &s = *impl_;
  s.owner();
  require(!s.compilation.layout.resident_raster_origin,
          "Resident fragment variant requires indexed draw_tile");
  const auto count = s.compilation.count;
  require(target && GPU_texture_dimensions(target) == 2 && !GPU_texture_is_array(target) &&
              GPU_texture_format(target) == gpu::TextureFormat::SFLOAT_32_32_32_32,
          "Fragment requires private2D RGBA32F target");
  require(source.size() >= 3 && source.size() % 3 == 0 &&
              source.size() <= std::numeric_limits<unsigned int>::max(),
          "Fragment triangle vertex count invalid");
  require(values.size() == count && (mappings.empty() || mappings.size() == count),
          "Fragment dynamic row count differs");
  if (s.compilation.layout.tangent_stream) {
    require(physical, "Fragment tangent matrix context missing");
    for (float x : physical->world_to_object)
      require(std::isfinite(x), "Nonfinite fragment world-to-object matrix");
  }
  if (s.compilation.diagnostic.normal_maps) {
    require(source.size() <= std::numeric_limits<int>::max(),
            "Fragment physical-facing vertex range exceeds draw API");
    require(physical && physical->object_infos && !physical->facing_batches.empty(),
            "Fragment Normal Map requires actual ObjectInfos, matrix and physical-facing batches");
    require(physical->facing_batches.size() <= 4,
            "Fragment physical-facing mesh must be prepartitioned into at most four batches");
    std::size_t end = 0;
    for (const auto &b : physical->facing_batches) {
      require(b.first == end && b.count && b.count % 3 == 0 &&
                  b.count <= std::numeric_limits<int>::max(),
              "Fragment physical-facing batches are not complete triangle ranges");
      end += b.count;
    }
    require(end == source.size(),
            "Fragment physical-facing batches do not cover the supplied mesh");
    for (float x : physical->world_to_object)
      require(std::isfinite(x), "Nonfinite fragment world-to-object matrix");
  }
  for (const auto &row : values)
    for (float x : row)
      require(std::isfinite(x), "Nonfinite fragment parameter");
  for (const auto &m : mappings)
    m.validate();

  // Diagnostic callers can provide raw mesh corners too. Repair directions in
  // a private copy; collapse only triangles with nonfinite raster/position data.
  std::vector<NativeFragmentVertex> repaired(source.begin(), source.end());
  for (std::size_t i = 0; i < repaired.size(); i += 3) {
    bool usable = true;
    for (int k = 0; k < 3; ++k) {
      auto &v = repaired[i + k];
      for (const auto *uv : {&v.raster_uv, &v.uv})
        for (float x : *uv)
          usable &= std::isfinite(x);
      for (float x : v.position)
        usable &= std::isfinite(x);
      const auto repair = [](auto &n, const auto &fallback) {
        const double length = std::hypot(double(n[0]), double(n[1]), double(n[2]));
        if (!(length > 0) || !std::isfinite(length))
          n = native_surface_unit(fallback);
      };
      repair(v.geometric_normal, std::array<float, 3>{0, 0, 1});
      repair(v.normal, v.geometric_normal);
      repair(v.initial_normal, v.normal);
      // Tangents are object-space, unlike the normal payload.
      auto local = native_surface_unit(v.normal);
      if (physical) {
        const float3x3 inverse(physical->world_to_object.data());
        const auto n = math::transpose(math::invert(inverse)) * float3(local.data());
        local = native_surface_unit({n.x, n.y, n.z});
      }
      // Stock GLSL explicitly maps this sentinel to the repaired original Ni.
      if (v.tangent != std::array<float, 4>{0, 0, 0, 1})
        v.tangent = native_surface_tangent(v.tangent, local);
    }
    if (!usable)
      for (int k = 0; k < 3; ++k) {
        auto &v = repaired[i + k];
        v.raster_uv = v.uv = {0, 0};
        v.position = {0, 0, 0};
      }
  }
  source = repaired;
  bool changed = !std::equal(values.begin(), values.end(), s.values.begin());
  std::copy(values.begin(), values.end(), s.values.begin());
  if (s.compilation.diagnostic.mapping_rows)
    for (std::size_t i = 0; i < count; ++i) {
      const auto rows = (mappings.empty() ? NativeMaterialMapping{} : mappings[i]).rows();
      for (int j = 0; j < 3; ++j) {
        auto &v = s.values[count + 3 * i + j];
        changed |= v != rows[j];
        v = rows[j];
      }
    }
  if (changed)
    GPU_storagebuf_update(s.parameters, s.values.data());
  if (s.compilation.diagnostic.normal_maps)
    GPU_storagebuf_update(s.object_infos, physical->object_infos);
  State state;
  gpu::FrameBuffer *raw = nullptr;
  GPU_framebuffer_ensure_config(&raw, {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(target)});
  std::unique_ptr<gpu::FrameBuffer, decltype(&GPU_framebuffer_free)> fbo(raw,
                                                                         GPU_framebuffer_free);
  char error[256]{};
  require(raw && GPU_framebuffer_check_valid(raw, error), error);
  const float clear[4]{};
  GPU_texture_clear(target, GPU_DATA_FLOAT, clear);
  GPU_framebuffer_bind_no_srgb(raw);
  GPU_viewport(0, 0, GPU_texture_width(target), GPU_texture_height(target));
  GPU_scissor_test(false);
  GPU_front_facing(false);
  GPU_blend(GPU_BLEND_NONE);
  GPU_depth_test(GPU_DEPTH_NONE);
  GPU_face_culling(GPU_CULL_NONE);
  GPU_stencil_test(GPU_STENCIL_NONE);
  GPU_color_mask(true, true, true, true);
  GPUVertFormat format{};
  const auto raster = GPU_vertformat_attr_add(
                 &format, "raster_uv", gpu::VertAttrType::SFLOAT_32_32),
             uv = GPU_vertformat_attr_add(&format, "authored_uv", gpu::VertAttrType::SFLOAT_32_32);
  std::array<unsigned int, 4> ids{};
  int i = 0;
  for (const char *name : {"world_P", "world_N", "world_Ni", "world_Ng"})
    ids[i++] = GPU_vertformat_attr_add(&format, name, gpu::VertAttrType::SFLOAT_32_32_32);
  const auto tangent = s.compilation.layout.tangent_stream ?
                           GPU_vertformat_attr_add(
                               &format, "object_tangent", gpu::VertAttrType::SFLOAT_32_32_32_32) :
                           0;
  std::unique_ptr<gpu::VertBuf, decltype(&GPU_vertbuf_discard)> vertices(
      GPU_vertbuf_create_with_format(format), GPU_vertbuf_discard);
  require(bool(vertices), "Fragment vertex buffer allocation failed");
  GPU_vertbuf_data_alloc(*vertices, source.size());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), raster, sizeof(NativeFragmentVertex), source[0].raster_uv.data());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), uv, sizeof(NativeFragmentVertex), source[0].uv.data());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), ids[0], sizeof(NativeFragmentVertex), source[0].position.data());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), ids[1], sizeof(NativeFragmentVertex), source[0].normal.data());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), ids[2], sizeof(NativeFragmentVertex), source[0].initial_normal.data());
  GPU_vertbuf_attr_fill_stride(
      vertices.get(), ids[3], sizeof(NativeFragmentVertex), source[0].geometric_normal.data());
  if (s.compilation.layout.tangent_stream)
    GPU_vertbuf_attr_fill_stride(
        vertices.get(), tangent, sizeof(NativeFragmentVertex), source[0].tangent.data());
  std::unique_ptr<gpu::Batch, decltype(&GPU_batch_discard)> batch(
      GPU_batch_create_ex(GPU_PRIM_TRIS, vertices.get(), nullptr, GPU_BATCH_OWNS_VBO),
      GPU_batch_discard);
  require(bool(batch), "Fragment batch allocation failed");
  vertices.release();
  auto *shader = GPU_material_get_shader(s.material);
  require(shader, "Fragment shader missing");
  GPU_batch_set_shader(batch.get(), shader);
  GPU_shader_bind(shader);
  const int parameters_binding = GPU_shader_get_ssbo_binding(shader, "native_parameters");
  if (parameters_binding >= 0)
    GPU_storagebuf_bind(s.parameters, parameters_binding);
  auto *ubo = GPU_material_uniform_buffer_get(s.material);
  if (ubo)
    GPU_uniformbuf_bind(ubo, GPU_shader_get_ubo_binding(shader, "node_tree"));
  const int object_binding = GPU_shader_get_ssbo_binding(shader, "native_object_infos");
  if (object_binding >= 0)
    GPU_storagebuf_bind(s.object_infos, object_binding);
  if (s.compilation.layout.tangent_stream) {
    require(physical, "Fragment tangent matrix context missing");
    GPU_shader_uniform_mat3(
        shader,
        "native_world_to_object",
        reinterpret_cast<const float (*)[3]>(physical->world_to_object.data()));
  }
  const auto unbind = qScopeGuard([&] {
    if (ubo)
      GPU_uniformbuf_unbind(ubo);
    if (object_binding >= 0)
      GPU_storagebuf_unbind(s.object_infos);
    if (parameters_binding >= 0)
      GPU_storagebuf_unbind(s.parameters);
    GPU_texture_unbind_all();
    GPU_shader_unbind();
  });
  for (const auto &texture : GPU_material_textures(s.material)) {
    require(texture.colorband && !texture.ima, "Fragment phase1 image binding is not allowed");
    require(*texture.colorband, "Fragment ramp texture missing");
    GPU_texture_bind(*texture.colorband,
                     GPU_shader_get_sampler_binding(shader, texture.sampler_name));
  }
  if (s.compilation.diagnostic.normal_maps) {
    for (const auto &range : physical->facing_batches) {
      GPU_front_facing(range.invert_front_face);
      GPU_batch_draw_range(batch.get(), range.first, range.count);
    }
  }
  else
    GPU_batch_draw(batch.get());
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
}

void NativeMaterialFragmentGpuProgram::draw_tile(blender::gpu::Texture *target,
                                                 const NativeMaterialFragmentGpuGeometry &resident,
                                                 int tile,
                                                 std::span<const std::array<float, 4>> values,
                                                 std::span<const NativeMaterialMapping> mappings,
                                                 bool back_face,
                                                 const NativeMaterialViewContext *view,
                                                 const NativeBakedAoInput *ao,
                                                 const NativeMaterialProjection *projection)
{
  using namespace blender;
  auto &s = *impl_;
  s.owner();
  auto &g = *resident.impl_;
  g.owner();
  if (s.compilation.diagnostic.image_context) {
    if (s.image_geometry.lock() != g.source || s.image_geometry_revision != g.source->revision()) {
      s.images.validate_uv(*g.source, s.compilation.layout.uv_name);
      s.image_geometry = g.source;
      s.image_geometry_revision = g.source->revision();
    }
    validate_retained_fragment_textures(s.material, s.images);
  }
  if (s.compilation.diagnostic.baked_ao_context) {
    if (!ao)
      native_baked_ao_reject(tile, "retained draw binding is pending");
    ao->validate(tile);
  }
  if (s.compilation.diagnostic.view_context) {
    if (!view)
      throw NativeFragmentContextError(
          NativeFragmentContextCode::View,
          "Native fragment requires a real validated view before draw");
    view->validate();
  }
  if (s.compilation.layout.generated_stream)
    validate_native_fragment_generated_context(*g.source);
  require(!back_face || s.compilation.layout.paired_physical_sides,
          "Back-face draw needs explicit paired physical layout");
  require(s.compilation.layout.resident_raster_origin,
          "Indexed fragment draw requires explicit resident raster-origin layout");
  require(target && GPU_texture_dimensions(target) == 2 && !GPU_texture_is_array(target) &&
              GPU_texture_format(target) == gpu::TextureFormat::SFLOAT_32_32_32_32,
          "Resident fragment requires private2D RGBA32F target");
  require(tile >= 1001, "Resident fragment target UDIM is invalid");
  const auto count = s.compilation.count;
  require(values.size() == count && (mappings.empty() || mappings.size() == count),
          "Resident fragment dynamic row count differs");
  for (const auto &row : values)
    for (float x : row)
      require(std::isfinite(x), "Nonfinite resident fragment parameter");
  for (const auto &m : mappings)
    m.validate();
  const auto matches = [&](const NativeFragmentGeometryStream &stream) {
    return std::any_of(stream.requests.begin(), stream.requests.end(), [&](const auto &r) {
      return s.compilation.layout.uv_name.empty() ?
                 r.kind == NativeFragmentUvKind::DefaultRender :
                 r.kind == NativeFragmentUvKind::Named && r.name == s.compilation.layout.uv_name;
    });
  };
  require(std::any_of(g.source->streams().begin(), g.source->streams().end(), matches),
          "Resident geometry has no exact logical source UV request for this program");
  const auto at = std::lower_bound(g.tiles.begin(),
                                   g.tiles.end(),
                                   tile,
                                   [](const auto &t, int number) { return t.number < number; });
  if (s.compilation.diagnostic.object_matrices_context && at != g.tiles.end() &&
      at->number == tile)
    for (std::size_t i = at->first; i < std::size_t(at->first) + at->count; ++i) {
      const auto &range = g.source->ranges()[g.range_order[i]];
      const auto &stream = g.source->streams()[range.stream];
      if (!matches(stream))
        continue;
      const auto owner = stream.object;
      g.object_matrices.prepare(owner, [&] {
        const auto matrices = native_fragment_image_object_matrices(g.source->objects()[owner],
                                                                    g.source->object_infos(owner));
        ObjectMatrices const *data = &matrices;
        std::shared_ptr<gpu::StorageBuf> buffer(
            GPU_storagebuf_create_ex(sizeof(matrices),
                                     data,
                                     GPU_USAGE_STATIC,
                                     "Native fragment actual BOX ObjectMatrices"),
            GPU_storagebuf_free);
        require(bool(buffer), "Resident BOX owner matrix allocation failed");
        g.diagnostic.storage_bytes = resource_sum(g.diagnostic.storage_bytes, sizeof(matrices));
        return buffer;
      });
    }
  auto *shader = GPU_material_get_shader(s.material);
  require(shader, "Resident fragment shader missing");
  bool changed = !std::equal(values.begin(), values.end(), s.values.begin());
  std::copy(values.begin(), values.end(), s.values.begin());
  if (s.compilation.diagnostic.mapping_rows)
    for (std::size_t i = 0; i < count; ++i) {
      const auto rows = (mappings.empty() ? NativeMaterialMapping{} : mappings[i]).rows();
      for (int j = 0; j < 3; ++j) {
        auto &v = s.values[count + 3 * i + j];
        changed |= v != rows[j];
        v = rows[j];
      }
    }
  if (changed)
    GPU_storagebuf_update(s.parameters, s.values.data());
  if (projection)
    require(s.compilation.layout.fill_projection,
            "Native fragment program has no Fill projection binding");
  if (s.projection) {
    const auto rows = projection ? projection->encoded() : NativeMaterialProjection::Rows{};
    if (rows != s.projection_rows) {
      s.projection_rows = rows;
      GPU_storagebuf_update(s.projection, s.projection_rows.data());
    }
  }
  State state;
  gpu::FrameBuffer *raw = nullptr;
  GPU_framebuffer_ensure_config(&raw, {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(target)});
  std::unique_ptr<gpu::FrameBuffer, decltype(&GPU_framebuffer_free)> fbo(raw,
                                                                         GPU_framebuffer_free);
  char error[256]{};
  require(raw && GPU_framebuffer_check_valid(raw, error), error);
  const float clear[4]{};
  GPU_texture_clear(target, GPU_DATA_FLOAT, clear);
  GPU_framebuffer_bind_no_srgb(raw);
  GPU_viewport(0, 0, GPU_texture_width(target), GPU_texture_height(target));
  GPU_scissor_test(false);
  GPU_front_facing(false);
  GPU_blend(GPU_BLEND_NONE);
  GPU_depth_test(GPU_DEPTH_NONE);
  GPU_face_culling(GPU_CULL_NONE);
  GPU_stencil_test(GPU_STENCIL_NONE);
  GPU_color_mask(true, true, true, true);
  GPU_shader_bind(shader);
  if (s.compilation.diagnostic.baked_ao_context)
    GPU_texture_bind(ao->lease->resource.texture,
                     GPU_shader_get_sampler_binding(shader, "native_project_ao"));
  if (s.compilation.diagnostic.view_context) {
    GPU_shader_uniform_4fv(shader, "native_view_position", view->position.data());
    GPU_shader_uniform_4fv(shader, "native_view_forward", view->forward.data());
  }
  if (s.compilation.layout.paired_physical_sides)
    GPU_shader_uniform_1f(shader, "native_normal_side", back_face ? -1.0f : 1.0f);
  const int projection_binding = GPU_shader_get_ssbo_binding(shader, "native_projection");
  if (projection_binding >= 0)
    GPU_storagebuf_bind(s.projection, projection_binding);
  const int parameters_binding = GPU_shader_get_ssbo_binding(shader, "native_parameters"),
            object_binding = GPU_shader_get_ssbo_binding(shader, "native_object_infos"),
            matrices_binding = GPU_shader_get_ssbo_binding(shader, "native_object_matrices");
  if (parameters_binding >= 0)
    GPU_storagebuf_bind(s.parameters, parameters_binding);
  auto *ubo = GPU_material_uniform_buffer_get(s.material);
  if (ubo)
    GPU_uniformbuf_bind(ubo, GPU_shader_get_ubo_binding(shader, "node_tree"));
  gpu::StorageBuf *bound_object = nullptr, *bound_matrices = nullptr;
  const auto unbind = qScopeGuard([&] {
    if (projection_binding >= 0)
      GPU_storagebuf_unbind(s.projection);
    if (ubo)
      GPU_uniformbuf_unbind(ubo);
    if (bound_object)
      GPU_storagebuf_unbind(bound_object);
    if (bound_matrices)
      GPU_storagebuf_unbind(bound_matrices);
    if (parameters_binding >= 0)
      GPU_storagebuf_unbind(s.parameters);
    GPU_texture_unbind_all();
    GPU_shader_unbind();
  });
  bind_retained_fragment_textures(s.material, shader, s.images);
  if (at != g.tiles.end() && at->number == tile)
    for (std::size_t i = at->first; i < std::size_t(at->first) + at->count; ++i) {
      const auto &range = g.source->ranges()[g.range_order[i]];
      const auto &stream = g.source->streams()[range.stream];
      if (!matches(stream))
        continue;
      const auto owner = stream.object;
      const auto &object = g.source->objects()[owner];
      if (matrices_binding >= 0) {
        auto *matrices = g.object_matrices.at(owner).get();
        if (bound_matrices != matrices) {
          GPU_storagebuf_bind(matrices, matrices_binding);
          bound_matrices = matrices;
        }
      }
      if (object_binding >= 0) {
        auto *info = g.objects[owner].get();
        if (bound_object != info) {
          GPU_storagebuf_bind(info, object_binding);
          bound_object = info;
        }
        if (!g.object_uploaded[owner]) {
          g.object_uploaded[owner] = true;
          ++g.diagnostic.object_uploads;
        }
      }
      if (s.compilation.diagnostic.object_context) {
        GPU_shader_uniform_mat4(
            shader,
            "native_owner_world_to_object",
            reinterpret_cast<const float (*)[4]>(object.world_to_object.data()));
        GPU_shader_uniform_3fv(
            shader, "native_owner_location", object.object_to_world.data() + 12);
      }
      else if (s.compilation.layout.tangent_stream) {
        std::array<float, 9> matrix{};
        for (int col = 0; col < 3; ++col)
          for (int row = 0; row < 3; ++row)
            matrix[col * 3 + row] = object.world_to_object[col * 4 + row];
        GPU_shader_uniform_mat3(
            shader, "native_world_to_object", reinterpret_cast<const float (*)[3]>(matrix.data()));
      }
      GPU_shader_uniform_2fv(shader, "native_raster_origin", range.raster_origin.data());
      GPU_front_facing(range.invert_front_face != back_face);
      auto *batch = g.streams[range.stream].batch.get();
      GPU_batch_set_shader(batch, shader);
      GPU_batch_draw_range(batch, int(range.first_index), int(range.index_count));
    }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
}
}  // namespace bts
