#pragma once
#include "BrushStencil.hh"
#include "MaterialValues.hh"
#include <string>
namespace bts {
struct ProjectionMaterial {
  BrushStencil transform;
  MaterialValues values;
  std::string name = "uniform color";
  bool weave = false;
  float density = 64;
  ProjectionMaterial()
  {
    values.base_color = {1, 1, 1, 1};
  }
  void validate() const
  {
    transform.validate();
    if (!std::isfinite(density) || density < 4 || density > 512)
      throw std::invalid_argument("Invalid projection weave density");
    for (float v : values.base_color)
      if (!std::isfinite(v) || std::abs(v) > 65504)
        throw std::invalid_argument("Invalid projected color");
    for (float v : values.normal)
      if (!std::isfinite(v) || std::abs(v) > 65504)
        throw std::invalid_argument("Invalid projected normal");
    for (float v : {values.metallic, values.roughness, values.bump, values.ambient_occlusion})
      if (!std::isfinite(v) || std::abs(v) > 65504)
        throw std::invalid_argument("Invalid projected data");
  }
  bool same_image(const ProjectionMaterial &other) const
  {
    return weave == other.weave && density == other.density && values == other.values &&
           transform.same_image(other.transform);
  }
  MaterialValues local(std::array<double, 2> uv) const
  {
    auto result = values;
    if (!weave)
      return result;
    const double x = (uv[0] + 1) * .5 * density, y = (uv[1] + 1) * .5 * density;
    const double thread = .5 + .5 * std::sin(6.283185307179586 * (x + y * .15));
    const double cross = .5 + .5 * std::cos(6.283185307179586 * y);
    const float relief = float(.65 * thread + .35 * cross);
    for (int c = 0; c < 3; ++c)
      result.base_color[c] = std::clamp(
          result.base_color[c] * (.55f + .7f * relief), -65504.f, 65504.f);
    result.roughness = std::clamp(values.roughness + .1f * (relief - .5f), 0.f, 1.f);
    result.bump = std::clamp(values.bump + .004f * (relief - .5f), -65504.f, 65504.f);
    return result;
  }
  std::optional<MaterialValues> sample(std::array<double, 2> screen) const
  {
    const auto uv = transform.coordinates(screen);
    if (!uv)
      return {};
    return local(*uv);
  }
};
struct ProjectionMapping {
  StencilMapping mapping;
  ProjectionMaterial source;
  std::optional<float> scalar_override;
  std::optional<MaterialValues> sample(std::array<double, 2> screen) const
  {
    auto value = source.sample(screen);
    if (value && scalar_override)
      value->roughness = *scalar_override;
    return value;
  }
  std::optional<MaterialValues> sample_uv(std::array<double, 2> uv) const
  {
    const auto screen = mapping.screen_uv(uv);
    return screen ? sample(*screen) : std::nullopt;
  }
};
}  // namespace bts
