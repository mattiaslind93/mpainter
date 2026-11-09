#pragma once
#include <array>
#include <string>
#include <string_view>
#include <stdexcept>
namespace bts {
// Texture-backed draw parameters avoid driver specialization of default-block
// uniforms (observed as 60-120 ms first-dispatch stalls after a property edit).
struct GpuLayerParameters {
  struct Field {
    const char *name, *type, *components;
  };
  static constexpr Field fields[] = {{"compare_channel", "int", "x"},
                                     {"compare_enabled", "ivec2", "xy"},
                                     {"compare_base", "vec3", "xyz"},
                                     {"raw_linear_source", "int", "x"},
                                     {"source_zero_opacity", "int", "x"},
                                     {"warp_identity", "int", "x"},
                                     {"warp_info", "vec4", "xyzw"},
                                     {"quick_editing", "int", "x"},
                                     {"quick_inverted", "int", "x"},
                                     {"content_pass", "int", "x"},
                                     {"mask_pass", "int", "x"},
                                     {"geometry_masked", "int", "x"},
                                     {"source_kind", "int", "x"},
                                     {"scalar_channel", "int", "x"},
                                     {"value_precision", "int", "x"},
                                     {"blend_mode", "int", "x"},
                                     {"has_mask", "int", "x"},
                                     {"layer_active", "int", "x"},
                                     {"source_wrap", "int", "x"},
                                     {"source_filter", "int", "x"},
                                     {"source_ignore_alpha", "int", "x"},
                                     {"surface_stride", "int", "x"},
                                     {"content_effects", "int", "x"},
                                     {"effect_count", "int", "x"},
                                     {"fill_resource_kind", "int", "x"},
                                     {"projection_mode", "int", "x"},
                                     {"projection_wrap", "int", "x"},
                                     {"projection_symmetry", "int", "x"},
                                     {"fill_bump_bias", "float", "x"},
                                     {"opacity_value", "float", "x"},
                                     {"mask_background", "float", "x"},
                                     {"projection_hardness", "float", "x"},
                                     {"depth_hardness", "float", "x"},
                                     {"cylinder_radians", "float", "x"},
                                     {"source_value", "vec3", "xyz"},
                                     {"map_u", "vec3", "xyz"},
                                     {"map_v", "vec3", "xyz"},
                                     {"rotation_x", "vec3", "xyz"},
                                     {"rotation_y", "vec3", "xyz"},
                                     {"rotation_z", "vec3", "xyz"},
                                     {"projection_offset", "vec3", "xyz"},
                                     {"projection_scale", "vec3", "xyz"},
                                     {"inverse_denominator", "vec3", "xyz"},
                                     {"projection_flags", "vec3", "xyz"},
                                     {"backface_options", "vec3", "xyz"},
                                     {"symmetry_center", "vec3", "xyz"},
                                     {"anchor_options", "vec4", "xyzw"},
                                     {"atom_options", "vec4", "xyzw"},
                                     {"symmetry_options", "vec4", "xyzw"},
                                     {"symmetry_flip", "ivec2", "xy"},
                                     {"surface_size", "ivec2", "xy"},
                                     {"precompose_source", "int", "x"}};
  static constexpr std::size_t warp_data_start = std::size(fields), warp_rows = 18 + 9 * 9 * 4;
  std::array<std::array<float, 4>, std::size(fields) + warp_rows> values{};
  auto &at(std::string_view name)
  {
    for (std::size_t i = 0; i < std::size(fields); ++i)
      if (name == fields[i].name)
        return values[i];
    throw std::logic_error("Unknown GPU layer parameter");
  }
  void set(std::string_view name, float value)
  {
    at(name)[0] = value;
  }
  void set2(std::string_view name, const int *value)
  {
    auto &v = at(name);
    for (int c = 0; c < 2; ++c)
      v[c] = value[c];
  }
  void set3(std::string_view name, const float *value)
  {
    auto &v = at(name);
    for (int c = 0; c < 3; ++c)
      v[c] = value[c];
  }
  void set4(std::string_view name, const float *value)
  {
    auto &v = at(name);
    for (int c = 0; c < 4; ++c)
      v[c] = value[c];
  }
  static std::string declarations()
  {
    std::string result = "#define WARP_DATA_START " + std::to_string(warp_data_start) + "\n";
    for (std::size_t i = 0; i < std::size(fields); ++i) {
      const auto &f = fields[i];
      result += "#define " + std::string(f.name) + " " + f.type +
                "(texelFetch(layer_parameters_img,ivec2(" + std::to_string(i) + ",0),0)." +
                f.components + ")\n";
    }
    return result;
  }
};
}  // namespace bts
