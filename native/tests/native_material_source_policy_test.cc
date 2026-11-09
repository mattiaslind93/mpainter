#include "NativeMaterialSourcePolicy.hh"
#include "NativeMaterialGpuCompiler.hh"
#include "NativeWorldNormalObjectContext.hh"
#include "NativeMaterialViewContext.hh"
#include "NativeWorldNormalSurface.hh"
#include "NativeFragmentImageReadiness.hh"
#include <bit>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
template<class F> void rejects(F function, std::string_view reason)
{
  try {
    function();
  }
  catch (const std::invalid_argument &error) {
    require(std::string_view(error.what()).find(reason) != std::string_view::npos,
            "Wrong unavailable-context diagnostic");
    return;
  }
  throw std::runtime_error("Missing physical context was silently admitted");
}
}  // namespace
int main()
{
  try {
    using namespace bts;
    for (const char *name : {"BTS_Geometry_Position",
                             "BTS_Geometry_Normal",
                             "BTS_Geometry_Incoming",
                             "BTS_Object_Color",
                             "BTS_Object_Random",
                             "BTS_Object_Alpha",
                             "BTS_Object_Location",
                             "BTS_Coord_Object",
                             "BTS_Coord_Generated",
                             "BTS_Coord_Normal"})
    {
      const auto context = native_material_attribute_context(name);
      require(context.surface && !context.uv && !context.ao,
              "World/object context accidentally requires a UV node or AO map");
      require(!native_material_compute_context_value(name, false).empty(),
              "Real context expression missing");
    }
    const auto incoming = native_material_attribute_context("BTS_Geometry_Incoming");
    require(incoming.view && !incoming.object && !incoming.facing,
            "Incoming must invalidate on camera changes");
    require(native_material_compute_context_value("BTS_Geometry_Incoming", true) ==
                "coordinate_incoming(bts_real_position)",
            "Incoming uses a projected shading point");
    const auto facing = native_material_attribute_context("BTS_Geometry_Backfacing");
    require(facing.surface && facing.facing && !facing.uv && !facing.view,
            "Physical side was confused with UV winding or camera context");
    const auto ao = native_material_attribute_context("BTS_Mesh_AO");
    require(ao.ao && !ao.uv && !ao.object && !ao.view,
            "Project AO map acquired a Texture Coordinate dependency");
    require(native_material_compute_context_value("BTS_Mesh_AO", true) == "vec3(bts_mesh_ao)",
            "AO does not use the project map");
    require(native_material_compute_context_value("BTS_Object_Alpha", true) ==
                "vec3(bts_object_color.a)",
            "Object alpha is fabricated");
    require(native_material_compute_context_value("BTS_Object_Location", true) ==
                "bts_object_location",
            "Object location is a shading point");
    require(native_material_compute_context_value("", true) == "bts_generated" &&
                native_material_compute_context_value("", false) == "vec4(bts_uv,1.0)",
            "Implicit Generated/UV context changed");
    require(native_material_fragment_context_value("BTS_Geometry_True Normal", true, true) ==
                "normalize(fragment_Ng)",
            "True Normal lost its independent geometric stream");
    require(native_material_fragment_context_value("BTS_Geometry_Backfacing", true, true) ==
                "vec3(native_normal_side<0.0?1.0:0.0)",
            "Paired physical side lost its explicit binding");
    rejects([] { native_material_compute_context_value("BTS_Geometry_Backfacing", false); },
            "physical surface facing");
    rejects([] { native_material_compute_context_value("BTS_Geometry_True Normal", false); },
            "geometric-normal binding");
    rejects(
        [] { native_material_fragment_context_value("BTS_Geometry_Backfacing", false, false); },
        "raster UV winding");
    rejects([] { native_material_fragment_context_value("BTS_Geometry_Incoming", false, true); },
            "actual view context");
    rejects([] { native_material_fragment_context_value("BTS_Mesh_AO", false, true); },
            "retained project AO map");
    rejects([] { native_material_compute_context_value("BTS_Object_Object Index", false); },
            "object/material binding");
    rejects([] { native_material_compute_context_value("BTS_Coord_Camera", false); },
            "unsupported geometry input");
    for (const char *name : {"BTS_Coord_Object",
                             "BTS_Object_Color",
                             "BTS_Object_Alpha",
                             "BTS_Object_Random",
                             "BTS_Object_Location"})
    {
      rejects([&] { native_material_fragment_context_value(name, false, true); },
              "captured resident owner context");
      require(!native_material_fragment_context_value(name, false, true, true).empty(),
              "Captured owner expression missing");
    }
    require(native_material_fragment_context_value("BTS_Coord_Object", true, true, true) ==
                "(native_owner_world_to_object*vec4(fragment_P,1.0)).xyz",
            "Object position lost the full inverse affine translation");
    require(native_material_fragment_context_value("BTS_Object_Color", true, true, true) ==
                    "native_object_infos[0].ob_color.rgb" &&
                native_material_fragment_context_value("BTS_Object_Color", false, true, true) ==
                    "native_object_infos[0].ob_color",
            "Object color/alpha payload differs");
    bool generated_rejected = false;
    try {
      native_material_fragment_context_value("BTS_Coord_Generated", false, true, true);
    }
    catch (const NativeFragmentContextError &error) {
      generated_rejected = error.code == NativeFragmentContextCode::GeneratedOrco;
    }
    require(generated_rejected, "Missing Generated payload lost its typed ORCO rejection");
    require(
        native_material_fragment_context_value("BTS_Coord_Generated", true, true, true, true) ==
                "fragment_generated" &&
            native_material_fragment_context_value(
                "BTS_Coord_Generated", false, true, true, true) == "vec4(fragment_generated,1.0)",
        "Generated does not read its supplied independent corner stream");
    for (const char *type : {"ShaderNodeTexNoise",
                             "ShaderNodeTexGabor",
                             "ShaderNodeTexWave",
                             "ShaderNodeTexVoronoi",
                             "ShaderNodeTexSky",
                             "ShaderNodeTexGradient",
                             "ShaderNodeTexMagic",
                             "ShaderNodeTexChecker",
                             "ShaderNodeTexBrick"})
      require(native_material_implicit_generated_texture(type),
              "Stock procedural ORCO default lost its source classification");
    require(!native_material_implicit_generated_texture("ShaderNodeTexImage") &&
                !native_material_implicit_generated_texture("ShaderNodeTexEnvironment"),
            "Image/environment default coordinates were confused with Generated");
    require(native_material_fragment_context_value(
                "BTS_Geometry_Incoming", true, true, false, false, true) ==
                "coordinate_incoming(g_data.P)",
            "Fragment Incoming does not use the physical stock sampling point");
    require(native_fragment_view_source().find("native_view_position.xyz-P") !=
                    std::string_view::npos &&
                native_fragment_view_source().find("fragment_P") == std::string_view::npos,
            "Fragment incoming helper ignores its point argument");
    for (const char *name : {"BTS_Coord_Camera", "BTS_Coord_Window", "BTS_Coord_Reflection"})
      rejects([&] { native_material_fragment_context_value(name, true, true, true, true, true); },
              name);
    rejects([] { native_material_fragment_context_value("BTS_Coord_Normal", true, true); },
            "captured resident owner matrices");
    const auto object_normal = native_material_fragment_context_value(
        "BTS_Coord_Normal", true, true, true);
    require(object_normal == "(normalize(fragment_N)*mat3(native_object_matrices[0].model))" &&
                native_material_fragment_context_value("BTS_Coord_Normal", false, true, true) ==
                    "vec4(" + object_normal + ",1.0)",
            "Texture Coordinate Normal lost raw stock model normal transform");
    require(native_material_fragment_object_matrices_attribute("BTS_Coord_Normal") &&
                !native_material_fragment_object_attribute("BTS_Coord_Normal") &&
                !native_material_fragment_object_matrices_attribute("BTS_Geometry_Normal"),
            "Coordinate Normal requested push/object-info state or aliased world Geometry Normal");
    bool missing_view = false;
    try {
      NativeMaterialViewContext{}.validate();
    }
    catch (const NativeFragmentContextError &error) {
      missing_view = error.code == NativeFragmentContextCode::View;
    }
    require(missing_view, "Default camera was accepted as real view data");
    require(native_fragment_push_bytes(
                native_fragment_push_fields(true, true, true, true, true)) == 128,
            "Owner/view push declarations exceed or misstate aligned portable budget");
    require(native_material_fragment_context_value(
                "BTS_Mesh_AO", true, true, false, false, false, true) ==
                "vec3(native_project_ao_get())",
            "Retained project AO expression does not use its supplied raster sampler");
    bool missing_ao = false;
    try {
      native_material_fragment_context_value("BTS_Mesh_AO", false, true);
    }
    catch (const NativeFragmentContextError &error) {
      missing_ao = error.code == NativeFragmentContextCode::BakedAo;
    }
    require(missing_ao, "Missing AO context lost its typed pre-GPU rejection");
    require(native_world_normal_surface_name(NativeWorldNormalSurfaceContract::SourceAuthored) ==
                    "source-authored" &&
                native_world_normal_surface_name(
                    NativeWorldNormalSurfaceContract::PainterUndisplaced) == "painter-undisplaced",
            "Effective surface contract identity differs");
    require(native_world_normal_surface_identity(
                "same-source", NativeWorldNormalSurfaceContract::SourceAuthored) !=
                native_world_normal_surface_identity(
                    "same-source", NativeWorldNormalSurfaceContract::PainterUndisplaced),
            "Different surface contracts alias one source cache identity");
    rejects(
        [] {
          native_world_normal_surface_name(static_cast<NativeWorldNormalSurfaceContract>(73));
        },
        "Unknown native world Normal surface contract");
    NativeFragmentImageReadiness images;
    int advances = 0;
    bool image_pending = false;
    try {
      images.require_ready(true);
    }
    catch (const NativeFragmentContextError &error) {
      image_pending = error.code == NativeFragmentContextCode::Images;
    }
    require(image_pending, "Missing image readiness lost typed pre-target rejection");
    require(!images.step(true,
                         [&] {
                           ++advances;
                           return false;
                         }) &&
                !images.ready(),
            "Partial image upload became ready");
    require(images.step(true,
                        [&] {
                          ++advances;
                          return true;
                        }) &&
                images.step(true,
                            [&] {
                              ++advances;
                              return false;
                            }) &&
                advances == 2,
            "Ready image upload was repeated or lost readiness");
    images.reset();
    require(images.step(false,
                        [&] {
                          ++advances;
                          return false;
                        }) &&
                advances == 2 && !images.ready(),
            "Image-free source requested an upload");
    NativeWorldNormalObjectContext stamp;
    std::vector<std::array<float, 4>> rows(24);
    rows[0][0] = std::bit_cast<float>(std::uint32_t(0xffc00001));
    rows[12][0] = std::bit_cast<float>(std::uint32_t(0xffc00002));
    require(stamp.observe(rows) && !stamp.observe(rows),
            "Exact opaque owner bits caused repeated invalidation");
    for (const auto [row, col] : {std::pair{0, 1},
                                  std::pair{4, 0},
                                  std::pair{8, 1},
                                  std::pair{11, 0},
                                  std::pair{11, 3},
                                  std::pair{12, 1},
                                  std::pair{16, 0},
                                  std::pair{20, 1},
                                  std::pair{23, 0},
                                  std::pair{23, 3}})
    {
      rows[row][col] += .125f;
      require(stamp.observe(rows) && !stamp.observe(rows),
              "Owner property/matrix change did not invalidate exactly once");
    }
    rows[9][0] = 123;
    rows[22][1] = 456;
    require(!stamp.observe(rows), "Generated texspace entered the resident property stamp");
    rows[11][3] = std::numeric_limits<float>::infinity();
    rejects([&] { stamp.observe(rows); }, "nonfinite");
    const auto bevel = native_material_node_context("ShaderNodeBevel");
    require(bevel.surface && !bevel.view && !bevel.uv && !bevel.object && !bevel.ao &&
                !bevel.facing,
            "EEVEE Bevel lost its real implicit surface normal");
    const auto warning = native_material_node_context_warning("ShaderNodeBevel");
    require(warning.find("EEVEE input-normal passthrough") != std::string_view::npos &&
                warning.find("Cycles ray-rounded bevel is not evaluated") !=
                    std::string_view::npos,
            "Bevel warning implies a rounded result");
    for (const char *type : {"ShaderNodeFresnel", "ShaderNodeLayerWeight"}) {
      const auto context = native_material_node_context(type);
      require(context.surface && context.view && !context.uv,
              "View-dependent normal node lost its surface/view context");
    }
    NativeMaterialCompilation layout;
    layout.parameter_count = 8;
    require(layout.logical_parameter_count() == 8, "Unmapped logical parameter count differs");
    layout.parameter_slots = {3, 7};
    require(layout.logical_parameter_count() == 2,
            "Physical remap storage was mistaken for the logical parameter count");
    std::cout << "NATIVE_SOURCE_POLICY_CPU PASS (no GPU/Qt/Blender runtime)\n";
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "NATIVE_SOURCE_POLICY_CPU FAIL " << error.what() << '\n';
    return 1;
  }
}
