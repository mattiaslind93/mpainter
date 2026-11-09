// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace bts {
enum class NativeFragmentContextCode { GeneratedOrco, View, BakedAo, Images };
class NativeFragmentContextError : public std::invalid_argument {
 public:
  NativeFragmentContextCode code;
  NativeFragmentContextError(NativeFragmentContextCode reason, const std::string &message)
      : std::invalid_argument(message), code(reason)
  {
  }
};
// Source context comes from the used output, independently of whether the
// artist added a Texture Coordinate node. These flags describe real bindings;
// they never authorize a fallback value for an unavailable binding.
struct NativeMaterialSourceContext {
  bool surface = false, object = false, view = false, ao = false, uv = false, facing = false;
};
inline NativeMaterialSourceContext native_material_attribute_context(std::string_view name)
{
  NativeMaterialSourceContext result;
  if (name.empty() || name == "BTS_Coord_UV")
    result.uv = true;
  else if (name == "BTS_Mesh_AO")
    result.ao = true;
  else if (name.starts_with("BTS_Object_") || name == "BTS_Coord_Object" ||
           name == "BTS_Coord_Generated" || name == "BTS_Coord_Normal")
    result.object = result.surface = true;
  else if (name.starts_with("BTS_Geometry_")) {
    result.surface = true;
    if (name == "BTS_Geometry_Incoming")
      result.view = true;
    if (name == "BTS_Geometry_Backfacing")
      result.facing = true;
  }
  else if (name == "BTS_Coord_Camera" || name == "BTS_Coord_Window" ||
           name == "BTS_Coord_Reflection")
    result.view = result.surface = true;
  return result;
}
// Expressions consumed by the real compute compiler, also inspectable without
// creating a GPU context. Physical P and incoming never use projected P.
inline std::string native_material_compute_context_value(std::string_view name, bool vec3)
{
  std::string value;
  if (name == "BTS_Mesh_AO")
    value = "vec3(bts_mesh_ao)";
  else if (name.empty())
    value = vec3 ? "bts_generated" : "bts_uv";
  else if (name == "BTS_Coord_UV")
    value = "bts_uv";
  else if (name == "BTS_Coord_Generated")
    value = "bts_generated";
  else if (name == "BTS_Coord_Object")
    value = "bts_object";
  else if (name == "BTS_Coord_Normal")
    value = "bts_object_normal";
  else if (name == "BTS_Geometry_Position")
    value = "bts_position";
  else if (name == "BTS_Geometry_Normal")
    value = "bts_normal";
  else if (name == "BTS_Geometry_Incoming")
    value = "coordinate_incoming(bts_real_position)";
  else if (name == "BTS_Object_Random")
    value = "vec3(bts_object_random)";
  else if (name == "BTS_Object_Color")
    return "bts_object_color";
  else if (name == "BTS_Object_Alpha")
    value = "vec3(bts_object_color.a)";
  else if (name == "BTS_Object_Location")
    value = "bts_object_location";
  else if (name == "BTS_Object_DisplacementScale")
    value = "vec3(bts_displacement_scale)";
  else if (name == "BTS_Geometry_Backfacing")
    throw std::invalid_argument(
        "Native Geometry Backfacing requires physical surface facing; the shading-normal atlas does not supply it");
  else if (name == "BTS_Geometry_True Normal")
    throw std::invalid_argument(
        "Native Geometry True Normal requires a geometric-normal binding; the shading-normal atlas does not supply it");
  else if (name.starts_with("BTS_Object_"))
    throw std::invalid_argument(
        "Native Object Info output has no retained object/material binding: " + std::string(name));
  else
    throw std::invalid_argument("Native material requires unsupported geometry input: " +
                                std::string(name));
  return vec3 ? value : "vec4(" + value + ",1.0)";
}
// Pinned node implementations call node_shader_gpu_default_tex_coord, which
// requests CD_ORCO when Vector is unlinked. Image and Environment differ.
inline bool native_material_implicit_generated_texture(std::string_view type)
{
  return type == "ShaderNodeTexNoise" || type == "ShaderNodeTexGabor" ||
         type == "ShaderNodeTexWave" || type == "ShaderNodeTexVoronoi" ||
         type == "ShaderNodeTexSky" || type == "ShaderNodeTexGradient" ||
         type == "ShaderNodeTexMagic" || type == "ShaderNodeTexChecker" ||
         type == "ShaderNodeTexBrick";
}
inline bool native_material_fragment_object_attribute(std::string_view name)
{
  return name == "BTS_Coord_Object" || name == "BTS_Object_Color" || name == "BTS_Object_Alpha" ||
         name == "BTS_Object_Random" || name == "BTS_Object_Location";
}
inline bool native_material_fragment_object_matrices_attribute(std::string_view name)
{
  return name == "BTS_Coord_Normal";
}
inline std::string native_material_fragment_context_value(std::string_view name,
                                                          bool vec3,
                                                          bool paired_physical_sides,
                                                          bool resident_object_context = false,
                                                          bool generated_context = false,
                                                          bool view_context = false,
                                                          bool baked_ao_context = false)
{
  std::string value;
  if (native_material_fragment_object_matrices_attribute(name)) {
    if (!resident_object_context)
      throw std::invalid_argument(
          "Fragment Texture Coordinate Normal requires captured resident owner matrices");
    // Stock node_tex_coord uses g_data.N * mat3(model). Its GPU node normalizes
    // only AFTER coordinate differentiation; the private adapter supplies that
    // final Normalize node rather than normalizing this raw object-space value.
    value = "(normalize(fragment_N)*mat3(native_object_matrices[0].model))";
  }
  else if (native_material_fragment_object_attribute(name)) {
    if (!resident_object_context)
      throw std::invalid_argument(
          "Fragment object attribute requires captured resident owner context: " +
          std::string(name));
    if (name == "BTS_Object_Color")
      return vec3 ? "native_object_infos[0].ob_color.rgb" : "native_object_infos[0].ob_color";
    if (name == "BTS_Object_Alpha")
      value = "vec3(native_object_infos[0].ob_color.a)";
    if (name == "BTS_Object_Random")
      value = "vec3(native_object_infos[0].random)";
    if (name == "BTS_Object_Location")
      value = "native_owner_location";
    if (name == "BTS_Coord_Object")
      value = "(native_owner_world_to_object*vec4(fragment_P,1.0)).xyz";
  }
  else if (name == "BTS_Coord_UV")
    value = "vec3(fragment_uv,0)";
  else if (name == "BTS_Geometry_Position")
    value = "fragment_P";
  else if (name == "BTS_Geometry_Normal")
    value = "normalize(fragment_N)";
  else if (name == "BTS_Geometry_True Normal")
    value = "normalize(fragment_Ng)";
  else if (name == "BTS_Geometry_Backfacing") {
    if (!paired_physical_sides)
      throw std::invalid_argument(
          "Fragment Geometry Backfacing requires explicit paired physical sides; raster UV winding is not surface facing");
    value = "vec3(native_normal_side<0.0?1.0:0.0)";
  }
  else if (name == "BTS_Coord_Generated") {
    if (!generated_context)
      throw NativeFragmentContextError(
          NativeFragmentContextCode::GeneratedOrco,
          "Fragment Generated coordinates require captured authoritative ORCO: BTS_Coord_Generated");
    value = "fragment_generated";
  }
  else if (name == "BTS_Geometry_Incoming") {
    if (!view_context)
      throw NativeFragmentContextError(NativeFragmentContextCode::View,
                                       "Fragment Geometry Incoming requires actual view context");
    value = "coordinate_incoming(g_data.P)";
  }
  else if (name == "BTS_Mesh_AO") {
    if (!baked_ao_context)
      throw NativeFragmentContextError(
          NativeFragmentContextCode::BakedAo,
          "Fragment Ambient Occlusion requires a retained project AO map; no ray evaluator or constant AO is supplied");
    value = "vec3(native_project_ao_get())";
  }
  else
    throw std::invalid_argument("Fragment attribute has no supplied UV/object/view binding: " +
                                std::string(name));
  return vec3 ? value : "vec4(" + value + ",1.0)";
}
// Blender EEVEE evaluates Bevel as input-N passthrough. An unlinked Normal
// obtains world_normals_get, so its reachable output requires real surface N.
inline NativeMaterialSourceContext native_material_node_context(std::string_view type)
{
  NativeMaterialSourceContext result;
  if (type == "ShaderNodeBevel")
    result.surface = true;
  if (type == "ShaderNodeFresnel" || type == "ShaderNodeLayerWeight")
    result.surface = result.view = true;
  return result;
}
inline std::string_view native_material_node_context_warning(std::string_view type)
{
  return type == "ShaderNodeBevel" ?
             "Bevel uses Blender EEVEE input-normal passthrough. Cycles ray-rounded bevel is not evaluated." :
             "";
}
}  // namespace bts
