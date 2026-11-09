#pragma once
#include "FillProjection.hh"
#include "FillTextureJSON.hh"
#include "SpatialProjection.hh"
#include "FillSpatialJSON.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
struct MaskFillMapping {
  FillProjection projection = [] {
    FillProjection p;
    p.alpha_mask[0] = false;
    return p;
  }();
  int filtering = 0, wrap = 0;
};
inline QJsonObject mask_fill_mapping_json(const MaskFillMapping &mapping)
{
  const auto &p = mapping.projection;
  return {{"use_alpha", p.alpha_mask[0]},
          {"texture", fill_texture_json(p.texture)},
          {"spatial", fill_spatial_json(p)},
          {"scale", QJsonArray{p.scale[0], p.scale[1]}},
          {"offset", QJsonArray{p.offset[0], p.offset[1]}},
          {"rotation", p.rotation},
          {"linked", p.linked},
          {"manipulator", p.manipulator},
          {"atom", p.atom},
          {"invert", p.invert},
          {"border_dots", p.border_dots},
          {"middle_dot", p.middle_dot},
          {"border_width", p.border_width},
          {"filtering", mapping.filtering},
          {"wrap", mapping.wrap}};
}
inline MaskFillMapping read_mask_fill_mapping(const QJsonValue &value)
{
  if (value.isUndefined())
    return {};
  const auto fail = []() { throw std::invalid_argument("Invalid mask fill mapping"); };
  if (!value.isObject())
    fail();
  const auto o = value.toObject();
  MaskFillMapping result;
  auto &p = result.projection;
  const auto boolean = [&](const char *key) {
    if (!o[key].isBool())
      fail();
    return o[key].toBool();
  };
  const auto number = [&](const QJsonValue &v, double low, double high) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < low || v.toDouble() > high)
      fail();
    return v.toDouble();
  };
  const auto scale = o["scale"].toArray(), offset = o["offset"].toArray();
  if (scale.size() != 2 || offset.size() != 2)
    fail();
  for (int i = 0; i < 2; ++i) {
    p.scale[i] = number(scale[i], -1000, 1000);
    p.offset[i] = number(offset[i], -1000, 1000);
  }
  p.rotation = number(o["rotation"], -360000, 360000);
  p.border_width = number(o["border_width"], 0, 1);
  p.linked = boolean("linked");
  p.manipulator = boolean("manipulator");
  p.atom = boolean("atom");
  p.invert = boolean("invert");
  p.border_dots = boolean("border_dots");
  p.middle_dot = boolean("middle_dot");
  for (const char *key : {"filtering", "wrap"}) {
    const auto v = o[key];
    if (!v.isDouble() || v.toDouble() != v.toInt(-1) || v.toInt() < 0 ||
        v.toInt() > (QString(key) == "wrap" ? 4 : 1))
      fail();
  }
  result.filtering = o["filtering"].toInt();
  result.wrap = o["wrap"].toInt();
  if (!o["use_alpha"].isUndefined() && !o["use_alpha"].isBool())
    fail();
  p.alpha_mask[0] = o["use_alpha"].isUndefined() ? true : o["use_alpha"].toBool();
  p.texture = read_fill_texture_json(o["texture"]);
  read_fill_spatial(p, o["spatial"]);
  validate_fill_projection(p);
  return result;
}
// Stable legacy wrap values: 0 repeat, 1 clamp, 2 black border. New shared
// editor adds 3 horizontal and 4 vertical repetition without reinterpreting files.
inline std::optional<std::array<double, 2>> mask_fill_wrapped(const MaskFillMapping &m,
                                                              std::array<double, 2> uv)
{
  const bool u = m.wrap == 0 || m.wrap == 3, v = m.wrap == 0 || m.wrap == 4;
  if (m.wrap != 1 && ((!u && (uv[0] < 0 || uv[0] > 1)) || (!v && (uv[1] < 0 || uv[1] > 1))))
    return {};
  for (int c = 0; c < 2; ++c) {
    if (c == 0 ? u : v)
      uv[c] -= std::floor(uv[c]);
    else
      uv[c] = std::clamp(uv[c], 0., std::nextafter(1., 0.));
  }
  return uv;
}
inline float mask_fill_texture(const MaskFillMapping &m, std::array<double, 2> uv)
{
  const auto wrapped = mask_fill_wrapped(m, uv);
  if (!wrapped)
    return 0;
  const auto &texture = *m.projection.texture;
  if (!texture.pixels)
    throw std::runtime_error("Mask fill texture is not resolved");
  const bool ru = m.wrap == 0 || m.wrap == 3, rv = m.wrap == 0 || m.wrap == 4;
  const auto at = [&](int x, int y) {
    x = ru ? (x % texture.width + texture.width) % texture.width :
             std::clamp(x, 0, texture.width - 1);
    y = rv ? (y % texture.height + texture.height) % texture.height :
             std::clamp(y, 0, texture.height - 1);
    const auto p = (*texture.pixels)[std::size_t(y) * texture.width + x];
    return p[0] * (m.projection.alpha_mask[0] ? p[3] : 1.f);
  };
  if (m.filtering == 1 || m.projection.filtering == 2)
    return at(int(std::floor((*wrapped)[0] * texture.width)),
              int(std::floor((*wrapped)[1] * texture.height)));
  const double x = (*wrapped)[0] * texture.width - .5, y = (*wrapped)[1] * texture.height - .5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  return std::lerp(std::lerp(at(ix, iy), at(ix + 1, iy), float(x - ix)),
                   std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), float(x - ix)),
                   float(y - iy));
}
inline float mask_fill_sample(const MaskFillMapping &mapping,
                              float uniform,
                              int x,
                              int y,
                              int width,
                              int height,
                              int tile,
                              const FillSurfaceAtlas *atlas = nullptr,
                              double cm_per_native = 1,
                              std::array<double, 2> native_per_uv = {1, 1})
{
  if (x < 0 || y < 0)
    return uniform;
  auto adjusted = mapping.projection;
  adjusted.sample_alpha = adjusted.alpha_mask[0];
  if (adjusted.physical) {
    adjusted.cm_per_native = cm_per_native;
    adjusted.native_per_uv = native_per_uv;
  }
  const auto &p = adjusted;
  if (fill_surface_projection(p)) {
    if (!atlas)
      return 0;
    const auto found = atlas->tiles.find(tile);
    if (found == atlas->tiles.end() || atlas->resolution < 1)
      return 0;
    const int rows = atlas->height ? atlas->height : atlas->resolution;
    const int gx = std::clamp(int((x + .5) * atlas->resolution / width), 0, atlas->resolution - 1),
              gy = std::clamp(int((y + .5) * rows / height), 0, rows - 1);
    const auto &surface = found->second.at(std::size_t(gy) * atlas->resolution + gx);
    if (p.mode == 1) {
      if (!surface.valid)
        return 0;
      if (p.texture && !p.atom) {
        const auto pixel = fill_texture_triplanar_sample(p, surface, atlas->extent);
        return pixel[0] * pixel[3];
      }
      return uniform * (p.atom ? fill_triplanar_sample(p, surface, atlas->extent) : 1);
    }
    const auto pixel = fill_spatial_sample(p, surface, *atlas, p.atom || bool(p.texture));
    if (p.texture && !p.atom)
      return pixel[0] * pixel[3];
    return uniform * pixel[0] * pixel[3];
  }
  if (p.mode == 6 && p.texture && !p.atom) {
    const auto pixel = fill_tile_match_sample(p, x, y, width, height, tile);
    return pixel[0] * pixel[3];
  }
  const int count = mapping.filtering == 1 || p.filtering > 0 ? 1 : 4;
  float sum = 0;
  for (int i = 0; i < count; ++i) {
    const double dx = count == 1 ? .5 :
                      (i & 1)    ? .75 :
                                   .25,
                 dy = count == 1 ? .5 :
                      (i & 2)    ? .75 :
                                   .25;
    const auto uv = fill_uv_map(
        p, (x + dx) / width + (tile - 1001) % 10, (y + dy) / height + (tile - 1001) / 10);
    if (p.texture && !p.atom)
      sum += mask_fill_texture(mapping, uv);
    else if (!p.atom)
      sum += uniform;
    else if (const auto mapped = mask_fill_wrapped(mapping, uv))
      sum += uniform * atom_pattern(p, (*mapped)[0], (*mapped)[1]);
  }
  return sum / count;
}
}  // namespace bts
