// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ExportFormat.hh"
#include "MaterialValues.hh"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
enum class ExportMapSource {
  Glossiness,
  GlossinessSquared,
  NormalDirectX,
  NormalOpenGL,
  MixedAo,
  Diffuse,
  Specular,
  F0,
  Reflection,
  InverseIor,
  Bump,
  Black,
  MaterialId
};
inline constexpr std::array<const char *, 13> export_map_labels{"Glossiness",
                                                                "Glossiness²",
                                                                "Normal DirectX",
                                                                "Normal OpenGL",
                                                                "Mixed AO",
                                                                "Diffuse",
                                                                "Specular",
                                                                "f0",
                                                                "Reflection",
                                                                "1/ior",
                                                                "Input Bump",
                                                                "Constant black",
                                                                "Material ID"};
inline constexpr std::array<const char *, 13> export_map_suffixes{"glossiness",
                                                                  "glossiness_squared",
                                                                  "normal_directx",
                                                                  "normal_opengl",
                                                                  "mixed_ao",
                                                                  "diffuse",
                                                                  "specular",
                                                                  "f0",
                                                                  "reflection",
                                                                  "inverse_ior",
                                                                  "bump",
                                                                  "black",
                                                                  "material_id"};
struct ExportMapOutput {
  std::string name;
  ExportMapSource source = ExportMapSource::Glossiness;
  bool alpha = false;
  std::optional<ExportFormat> format;
  bool operator==(const ExportMapOutput &) const = default;
};
inline void validate_export_map(const ExportMapOutput &map)
{
  const int source = int(map.source);
  if (source < 0 || source >= int(export_map_labels.size()) ||
      map.name.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid derived output map");
}
inline bool export_map_normal(ExportMapSource source)
{
  return source == ExportMapSource::NormalDirectX || source == ExportMapSource::NormalOpenGL;
}
inline bool export_map_color(ExportMapSource source)
{
  return source == ExportMapSource::Diffuse || source == ExportMapSource::Specular ||
         source == ExportMapSource::Reflection;
}
inline int export_map_components(ExportMapSource source)
{
  return export_map_normal(source) || export_map_color(source) ||
                 source == ExportMapSource::Black || source == ExportMapSource::MaterialId ?
             3 :
             1;
}
// Values are in the working space; the writer encodes color outputs exactly once.
inline std::array<float, 3> export_map_value(ExportMapSource source,
                                             const MaterialValues &value,
                                             std::array<float, 3> normal,
                                             float ao)
{
  const float metallic = std::clamp(value.metallic, 0.f, 1.f),
              roughness = std::clamp(value.roughness, 0.f, 1.f), gloss = 1 - roughness;
  std::array<float, 3> pixel{};
  switch (source) {
    case ExportMapSource::Glossiness:
      pixel.fill(gloss);
      break;
    case ExportMapSource::GlossinessSquared:
      pixel.fill(gloss * gloss);
      break;
    case ExportMapSource::NormalDirectX:
      normal[1] = 1 - normal[1];
      [[fallthrough]];
    case ExportMapSource::NormalOpenGL:
      return normal;
    case ExportMapSource::MixedAo:
      pixel.fill(ao);
      break;
    case ExportMapSource::Diffuse:
      for (int c = 0; c < 3; ++c)
        pixel[c] = value.base_color[c] * (1 - metallic);
      break;
    case ExportMapSource::Specular:
      for (int c = 0; c < 3; ++c)
        pixel[c] = std::lerp(.04f, value.base_color[c], metallic);
      break;
    case ExportMapSource::F0:
      pixel.fill(std::lerp(.04f, 1.f, metallic));
      break;
    case ExportMapSource::Reflection:
      for (int c = 0; c < 3; ++c)
        pixel[c] = std::lerp(1.f, value.base_color[c], metallic);
      break;
    case ExportMapSource::InverseIor:
      pixel.fill(1 / std::lerp(1.4f, 100.f, metallic));
      break;
    case ExportMapSource::Bump:
      pixel.fill(value.bump);
      break;
    case ExportMapSource::MaterialId:  // Background only; ownership is evaluated by
                                       // TextureDocument.
    case ExportMapSource::Black:
      break;
    default:
      throw std::invalid_argument("Unknown derived map source");
  }
  return pixel;
}
}  // namespace bts
