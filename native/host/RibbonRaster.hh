#pragma once
#include "ResourceAvailability.hh"
#include "SurfacePathRaster.hh"
#include "RibbonSettings.hh"
#include "QuickMask.hh"
#include "PaintRasterSource.hh"
#include "PaintRecording.hh"
#include "PathTriangleBounds.hh"
#include <map>
#include <set>
namespace bts {
namespace ribbon_detail {
using V = PathVector;
inline double dot(V a, V b)
{
  double s = 0;
  for (int i = 0; i < 3; ++i)
    s += a[i] * b[i];
  return s;
}
inline V unit(V a)
{
  const double l = path_length(a);
  if (l < 1e-12)
    return {};
  for (double &v : a)
    v /= l;
  return a;
}
inline V sum(V a, V b)
{
  for (int i = 0; i < 3; ++i)
    a[i] += b[i];
  return a;
}
inline V cross(V a, V b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
struct Piece {
  V a, b, dir, side, normal, start_plane, end_plane, start_side, end_side;
  double length = 0, arc = 0, width_a = 0, width_b = 0, flow_a = 1, flow_b = 1;
  bool first = false, last = false;
};
struct Field {
  std::vector<Piece> pieces;
  RibbonLayout layout;
};
template<class Path>
inline Field field(const Path &path,
                   double extent,
                   const std::vector<SurfacePathDisplaySample> &samples)
{
  Field result;
  if (samples.size() < 2)
    return result;
  double arc = 0;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const auto &a = samples[i - 1].point, &b = samples[i].point;
    Piece p;
    p.a = a.position;
    p.b = b.position;
    p.length = path_length(path_delta(p.b, p.a));
    if (p.length < 1e-10)
      continue;
    p.dir = unit(path_delta(p.b, p.a));
    p.normal = unit(sum(a.normal, b.normal));
    p.side = unit(cross(p.normal, p.dir));
    if (path_length(p.side) < .5)
      continue;
    p.arc = arc;
    p.width_a = extent * path.brush.diameter_at(a.pressure) / 100;
    p.width_b = extent * path.brush.diameter_at(b.pressure) / 100;
    p.flow_a = path.brush.flow_at(a.pressure) / 100;
    p.flow_b = path.brush.flow_at(b.pressure) / 100;
    arc += p.length;
    result.pieces.push_back(p);
  }
  result.layout = RibbonLayout::make(
      path.ribbon, arc, extent * path.brush.diameter / 100, path.closed);
  if (result.layout.period <= 0)
    return result;
  const double scale = result.layout.width / (extent * path.brush.diameter / 100);
  for (std::size_t i = 0; i < result.pieces.size(); ++i) {
    auto &p = result.pieces[i];
    p.width_a *= scale;
    p.width_b *= scale;
    p.first = i == 0 && !path.closed;
    p.last = i + 1 == result.pieces.size() && !path.closed;
    const auto &prev = result.pieces[(i + result.pieces.size() - 1) % result.pieces.size()],
               &next = result.pieces[(i + 1) % result.pieces.size()];
    p.start_plane = p.first ? p.dir : unit(sum(prev.dir, p.dir));
    p.end_plane = p.last ? p.dir : unit(sum(p.dir, next.dir));
    p.start_side = p.first ? p.side : prev.side;
    p.end_side = p.last ? p.side : next.side;
  }
  return result;
}
inline std::optional<std::array<double, 2>> coordinates(const Piece &p,
                                                        const RibbonSettings &s,
                                                        V position)
{
  const auto d = path_delta(position, p.a);
  const double along = dot(d, p.dir), t = std::clamp(along / p.length, 0., 1.),
               radius = std::lerp(p.width_a, p.width_b, t) * .5;
  if (radius <= 1e-12)
    return {};
  const double across = dot(d, p.side), depth = dot(d, p.normal);
  if (std::abs(depth) > radius || std::abs(across) > radius)
    return {};
  if (s.corner == RibbonCorner::Cut) {
    if (along < 0 || along > p.length)
      return {};
  }
  else if (s.corner == RibbonCorner::Round) {
    if ((p.first && along < 0) || (p.last && along > p.length))
      return {};
    const double outside = along < 0 ? along : along > p.length ? along - p.length : 0;
    if (outside * outside + across * across > radius * radius)
      return {};
  }
  else {
    if ((p.first && along < 0) || (p.last && along > p.length) || along < -radius * 4 ||
        along > p.length + radius * 4 || dot(d, p.start_plane) < -1e-9 ||
        dot(path_delta(position, p.b), p.end_plane) > 1e-9)
      return {};
    if (s.corner == RibbonCorner::Bevel) {
      if (!p.first && std::abs(dot(d, sum(p.start_side, p.side))) >
                          radius * (1 + dot(p.start_side, p.side)) + 1e-9)
        return {};
      if (!p.last && std::abs(dot(path_delta(position, p.b), sum(p.end_side, p.side))) >
                         radius * (1 + dot(p.end_side, p.side)) + 1e-9)
        return {};
    }
  }
  return std::array<double, 2>{p.arc + t * p.length, across / radius};
}
struct Texel {
  std::array<float, 4> rgba{};
  double distance = 1e100;
  std::size_t piece = 0;
  const Field *field = nullptr;
};

struct Patch {
  Texel sample;
  std::size_t first = 0, last = 0;
};
inline bool closer(double next, double current)
{
  return next + 1e-12 < current;
}
inline bool connected(const Field &field,
                      std::size_t previous,
                      std::size_t next,
                      bool wrap = false)
{
  const auto &a = field.pieces[previous], &b = field.pieces[next];
  const double gap = b.arc - (a.arc + a.length) + (wrap ? field.layout.total : 0);
  return gap <= (a.width_b + b.width_a) * .5 + 1e-12;
}
inline void append_patch(std::vector<Patch> &patches, Texel sample)
{
  if (!patches.empty() && patches.back().sample.field == sample.field &&
      connected(*sample.field, patches.back().last, sample.piece))
  {
    auto &patch = patches.back();
    patch.last = sample.piece;
    if (closer(sample.distance, patch.sample.distance))
      patch.sample = sample;
  }
  else
    patches.push_back({sample, sample.piece, sample.piece});
}
inline Texel composite_patches(std::vector<Patch> &patches, bool closed, RibbonAlpha policy)
{
  Texel selected;
  for (std::size_t first = 0; first < patches.size();) {
    std::size_t end = first + 1;
    while (end < patches.size() && patches[end].sample.field == patches[first].sample.field)
      ++end;
    bool merge =
        closed && end > first + 1 &&
        connected(*patches[first].sample.field, patches[end - 1].last, patches[first].first, true);
    if (merge && closer(patches[end - 1].sample.distance, patches[first].sample.distance))
      patches[first].sample = patches[end - 1].sample;
    for (std::size_t i = first; i < end - std::size_t(merge); ++i) {
      auto sample = patches[i].sample;
      auto &color = sample.rgba;
      if (selected.field) {
        if (policy == RibbonAlpha::Max && color[3] < selected.rgba[3])
          continue;
        if (policy == RibbonAlpha::Normal) {
          const float alpha = color[3] + selected.rgba[3] * (1 - color[3]);
          for (int k = 0; k < 3; ++k)
            color[k] = (color[k] * color[3] +
                        selected.rgba[k] * selected.rgba[3] * (1 - color[3])) /
                       alpha;
          color[3] = alpha;
        }
        if (policy == RibbonAlpha::Add) {
          const float alpha = std::min(1.f, color[3] + selected.rgba[3]);
          for (int k = 0; k < 3; ++k)
            color[k] = (color[k] * color[3] + selected.rgba[k] * selected.rgba[3]) /
                       (color[3] + selected.rgba[3]);
          color[3] = alpha;
        }
      }
      selected = sample;
    }
    first = end;
  }
  return selected;
}
using Raster = std::map<int, std::map<std::uint64_t, Texel>>;
inline std::vector<std::pair<const Field *, std::size_t>> candidates_for(
    const PaintPixels::SurfaceTriangle &tri, const std::vector<Field> &fields)
{
  std::vector<std::pair<const Field *, std::size_t>> candidates;
  V low = tri.position[0], high = low;
  for (const auto &p : tri.position)
    for (int k = 0; k < 3; ++k) {
      low[k] = std::min(low[k], p[k]);
      high[k] = std::max(high[k], p[k]);
    }
  for (const auto &field : fields)
    for (std::size_t index = 0; index < field.pieces.size(); ++index) {
      const auto &p = field.pieces[index];
      const double margin = std::max(p.width_a, p.width_b) * 2;
      bool overlap = true;
      for (int k = 0; k < 3; ++k)
        overlap &= std::max(p.a[k], p.b[k]) + margin >= low[k] &&
                   std::min(p.a[k], p.b[k]) - margin <= high[k];
      if (overlap)
        candidates.emplace_back(&field, index);
    }
  return candidates;
}
template<class Path>
inline Raster raster(const Path &path,
                     const std::vector<Field> &fields,
                     const std::vector<PaintPixels::SurfaceTriangle> &triangles,
                     int resolution,
                     const std::set<int> &tiles,
                     const std::shared_ptr<const QuickMaskField> &mask,
                     PaintPixels::Bounds clip = {})
{
  Raster output;
  std::uint64_t work = 0, covered = 0;
  for (const auto &tri : triangles) {
    const auto &a = tri.uv[0], &b = tri.uv[1], &c = tri.uv[2];
    const double ux = b[0] - a[0], uy = b[1] - a[1], vx = c[0] - a[0], vy = c[1] - a[1],
                 det = ux * vy - uy * vx;
    if (std::abs(det) < 1e-20)
      continue;
    // Broad-phase once per UV triangle: skip path pieces whose expanded world bounds cannot touch
    // it.
    const auto candidates = candidates_for(tri, fields);
    if (candidates.empty())
      continue;
    for (int tile : tiles) {
      const int tile_u = (tile - 1001) % 10, tile_v = (tile - 1001) / 10;
      const auto bound = [&](double x) { return int(std::clamp(x, 0., double(resolution))); };
      int x0 = bound(std::floor((std::min({a[0], b[0], c[0]}) - tile_u) * resolution)),
          x1 = bound(std::ceil((std::max({a[0], b[0], c[0]}) - tile_u) * resolution)),
          y0 = bound(std::floor((std::min({a[1], b[1], c[1]}) - tile_v) * resolution)),
          y1 = bound(std::ceil((std::max({a[1], b[1], c[1]}) - tile_v) * resolution));
      if (!clip.empty()) {
        x0 = std::max(x0, clip.x0);
        y0 = std::max(y0, clip.y0);
        x1 = std::min(x1, clip.x1);
        y1 = std::min(y1, clip.y1);
      }
      if (x0 >= x1 || y0 >= y1)
        continue;
      work = resource_sum(work,
                          resource_product(resource_product(x1 - x0, y1 - y0), candidates.size()));
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          const double u = (x + .5) / resolution + tile_u, v = (y + .5) / resolution + tile_v,
                       w1 = ((u - a[0]) * vy - (v - a[1]) * vx) / det,
                       w2 = (ux * (v - a[1]) - uy * (u - a[0])) / det;
          const std::array<double, 3> weights{1 - w1 - w2, w1, w2};
          if (*std::min_element(weights.begin(), weights.end()) < -1e-9)
            continue;
          V position{};
          for (int k = 0; k < 3; ++k)
            for (int i = 0; i < 3; ++i)
              position[k] += weights[i] * tri.position[i][k];
          std::vector<Patch> patches;
          for (const auto &[field, index] : candidates) {
            const auto &piece = field->pieces[index];
            const auto local = coordinates(piece, path.ribbon, position);
            if (!local)
              continue;
            const auto image = field->layout.coordinates(path.ribbon, (*local)[0], (*local)[1]);
            if (!image)
              continue;
            auto color = path.ribbon.image->sample((*image)[0], (*image)[1]);
            const double distance = std::abs((*local)[1]);
            color[3] *= std::lerp(piece.flow_a,
                                  piece.flow_b,
                                  std::clamp(((*local)[0] - piece.arc) / piece.length, 0., 1.));
            if (color[3] <= 1e-8f)
              continue;
            append_patch(patches, {color, distance, index, field});
          }
          auto selected = composite_patches(patches, path.closed, path.ribbon.alpha);
          const bool found = selected.field != nullptr;
          if (!found)
            continue;
          selected.rgba[3] *= mask ? mask->sample_uv(u, v, resolution) : 1;
          if (selected.rgba[3] <= 1e-8f)
            continue;
          auto &page = output[tile];
          const auto key = std::uint64_t(y) * resolution + x;
          const auto old = page.find(key);
          if (old == page.end()) {
            page.emplace(key, selected);
          }
          else if (selected.distance < old->second.distance)
            old->second = selected;
        }
    }
  }
  return output;
}
template<class Path>
inline TilePaintPixels pixels(
    const Raster &raster, int resolution, const Path &path, int mode, int user = -1)
{
  TilePaintPixels output;
  for (const auto &[tile, texels] : raster) {
    for (int channel = 0; channel < (mode == 0 ? 5 : 1); ++channel) {
      if (mode == 0 && !path.brush.channels[channel])
        continue;
      const int target = mode == 0 ? channel : 2;
      std::map<std::uint32_t, std::vector<PaintPixels::Pixel>> pages;
      for (const auto &[key, texel] : texels) {
        const int x = key % resolution, y = key / resolution;
        const auto page_key = (std::uint32_t(y / 64) << 16) | std::uint32_t(x / 64);
        auto &page = pages[page_key];
        if (page.empty())
          page.resize(4096);
        const auto &color = texel.rgba;
        const float alpha = color[3];
        std::array<float, 3> value{};
        if (mode == 1)
          value.fill(path.brush.values.ambient_occlusion);
        else if (mode == 2)
          value.fill(path.brush.user_values[user]);
        else if (mode == 3)
          value.fill(path.brush.displacement_value);
        else
          switch (channel) {
            case 0:
              for (int k = 0; k < 3; ++k)
                value[k] = color[k];
              break;
            case 1:
              value.fill(path.brush.values.metallic);
              break;
            case 2:
              value.fill(path.brush.values.roughness);
              break;
            case 3:
              value = path.brush.values.normal;
              break;
            case 4:
              value.fill(path.brush.values.bump);
              break;
          }
        auto &pixel = page[(y % 64) * 64 + x % 64];
        for (int k = 0; k < 3; ++k)
          pixel[k] = value[k] * alpha;
        pixel[3] = alpha;
      }
      for (const auto &[key, page] : pages)
        output.assign_page(resolution, target, key, page, tile);
    }
  }
  return output;
}
}  // namespace ribbon_detail

struct RibbonPathGeometry {
  SurfacePath path;
  std::vector<ribbon_detail::Field> fields;
  std::vector<PaintPixels::SurfaceTriangle> triangles;
  std::vector<std::vector<std::array<int, 2>>> candidates;
  std::vector<std::array<double, 4>> uv_bounds;
  std::shared_ptr<const QuickMaskField> mask;
};
struct RibbonPathGrid {
  std::shared_ptr<const RibbonPathGeometry> geometry;
  int size = 0, tile = 1001;
  std::map<std::uint32_t, std::vector<int>> pages;
};
struct RibbonPathSource final : PaintRasterSource {
  std::shared_ptr<const RibbonPathGrid> grid;
  int mode = 0, user = -1;
  std::array<float, 3> value(int channel) const
  {
    const auto &brush = grid->geometry->path.brush;
    std::array<float, 3> result{};
    if (mode == 1)
      result.fill(brush.values.ambient_occlusion);
    else if (mode == 2)
      result.fill(brush.user_values[user]);
    else if (mode == 3)
      result.fill(brush.displacement_value);
    else
      switch (channel) {
        case 0:
          std::copy_n(brush.values.base_color.begin(), 3, result.begin());
          break;
        case 1:
          result.fill(brush.values.metallic);
          break;
        case 2:
          result.fill(brush.values.roughness);
          break;
        case 3:
          result = brush.values.normal;
          break;
        case 4:
          result.fill(brush.values.bump);
          break;
      }
    return result;
  }
  PaintPixels evaluate_cpu() const override
  {
    const auto &g = *grid->geometry;
    PaintPixels result;
    for (const auto &[key, indices] : grid->pages) {
      std::vector<PaintPixels::SurfaceTriangle> triangles;
      triangles.reserve(indices.size());
      for (int index : indices)
        triangles.push_back(g.triangles[index]);
      const int x = int(key % 65536) * 64, y = int(key / 65536) * 64;
      const auto raster = ribbon_detail::raster(
          g.path, g.fields, triangles, grid->size, {grid->tile}, g.mask, {x, y, x + 64, y + 64});
      const auto pixels = ribbon_detail::pixels(raster, grid->size, g.path, mode, user);
      for (int c = 0; c < 5; ++c) {
        const auto page = pixels.tile(grid->tile).page_data(c, key);
        if (!page.empty())
          result.assign_page(grid->size, c, key, page);
      }
    }
    return result;
  }
  std::vector<const PaintPixels *> cpu_inputs() const override
  {
    std::vector<const PaintPixels *> inputs;
    if (grid->geometry->mask)
      for (const auto &[tile, pixels] : grid->geometry->mask->pixels.tiles())
        inputs.push_back(&pixels);
    return inputs;
  }
};
inline SurfacePathInk record_ribbon_path(const SurfacePath &path,
                                         std::vector<ribbon_detail::Field> fields,
                                         std::vector<PaintPixels::SurfaceTriangle> triangles,
                                         int resolution,
                                         const std::array<int, 8> &user_sizes,
                                         const std::set<int> &tiles,
                                         std::shared_ptr<const QuickMaskField> mask,
                                         int displacement_resolution)
{
  auto geometry = std::make_shared<RibbonPathGeometry>();
  geometry->path = path;
  geometry->fields = std::move(fields);
  geometry->mask = std::move(mask);
  for (const auto &triangle : triangles) {
    const auto candidates = ribbon_detail::candidates_for(triangle, geometry->fields);
    if (candidates.empty())
      continue;
    const auto &a = triangle.uv[0], &b = triangle.uv[1], &c = triangle.uv[2];
    if (std::abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-20)
      continue;
    std::array<PathClipVertex, 3> clip_triangle;
    for (int i = 0; i < 3; ++i)
      clip_triangle[i] = {triangle.position[i], triangle.uv[i]};
    std::optional<std::array<double, 4>> uv_bounds;
    for (const auto &[field, index] : candidates) {
      const auto &piece = field->pieces[index];
      const double margin = std::max(piece.width_a, piece.width_b) * 2 + 1e-6;
      std::vector<PathClipPlane> planes;
      for (int k = 0; k < 3; ++k) {
        PathVector normal{};
        normal[k] = 1;
        planes.push_back({normal, std::max(piece.a[k], piece.b[k]) + margin});
        normal[k] = -1;
        planes.push_back({normal, -std::min(piece.a[k], piece.b[k]) + margin});
      }
      if (const auto bounds = path_triangle_bounds(clip_triangle, planes)) {
        if (!uv_bounds)
          uv_bounds = bounds;
        else
          for (int k = 0; k < 2; ++k) {
            (*uv_bounds)[k] = std::min((*uv_bounds)[k], (*bounds)[k]);
            (*uv_bounds)[k + 2] = std::max((*uv_bounds)[k + 2], (*bounds)[k + 2]);
          }
      }
    }
    if (!uv_bounds)
      continue;
    geometry->uv_bounds.push_back(*uv_bounds);
    geometry->triangles.push_back(triangle);
    auto &indices = geometry->candidates.emplace_back();
    for (const auto &[field, index] : candidates)
      indices.push_back({int(field - geometry->fields.data()), int(index)});
  }
  std::map<std::pair<int, int>, std::shared_ptr<const RibbonPathGrid>> grids;
  const auto render = [&](int size, int mode, int user = -1) {
    TilePaintPixels result;
    for (int tile : tiles) {
      auto &grid = grids[{size, tile}];
      if (!grid) {
        auto value = std::make_shared<RibbonPathGrid>();
        value->geometry = geometry;
        value->size = size;
        value->tile = tile;
        const int tile_u = (tile - 1001) % 10, tile_v = (tile - 1001) / 10;
        for (std::size_t i = 0; i < geometry->triangles.size(); ++i) {
          const auto &bounds = geometry->uv_bounds[i];
          const auto bound = [&](double v) { return int(std::clamp(v, 0., double(size))); };
          const int x0 = bound(std::floor((bounds[0] - tile_u) * size)),
                    x1 = bound(std::ceil((bounds[2] - tile_u) * size));
          const int y0 = bound(std::floor((bounds[1] - tile_v) * size)),
                    y1 = bound(std::ceil((bounds[3] - tile_v) * size));
          if (x0 >= x1 || y0 >= y1)
            continue;
          for (int y = y0 / 64; y <= (y1 - 1) / 64; ++y)
            for (int x = x0 / 64; x <= (x1 - 1) / 64; ++x)
              value->pages[(std::uint32_t(y) << 16) | x].push_back(int(i));
        }
        grid = std::move(value);
      }
      PaintPixels::PageVersions versions;
      for (int c = 0; c < 5; ++c)
        if (mode == 0 ? path.brush.channels[c] : c == 2)
          for (const auto &[key, indices] : grid->pages)
            versions[c][key] = 1;
      auto source = std::make_shared<RibbonPathSource>();
      source->grid = grid;
      source->mode = mode;
      source->user = user;
      result.assign_tile(PaintPixels::from_source(size, std::move(source), versions), tile);
    }
    return result;
  };
  SurfacePathInk output;
  output.primary.paint = render(resolution, 0);
  if (path.brush.ao)
    output.primary.ao = render(resolution, 1);
  if (path.brush.displacement)
    output.displacement = render(displacement_resolution ? displacement_resolution : resolution,
                                 3);
  for (int slot = 0; slot < 8; ++slot)
    if (path.brush.users[slot])
      output.users[slot] = render(user_sizes[slot], 2, slot);
  return output;
}
template<class Path>
inline SurfacePathInk raster_ribbon_path(const Path &path,
                                         double extent,
                                         int resolution,
                                         const std::array<int, 8> &user_resolutions,
                                         const std::set<int> &tiles,
                                         const SurfacePathContact &contact,
                                         std::shared_ptr<const QuickMaskField> mask = {},
                                         int displacement_resolution = 0,
                                         bool record_source = record_gpu_paint)
{
  path.validate();
  path.ribbon.validate();
  if (path.brush.diameter <= 0 || path.brush.flow <= 0 || path.brush.stroke_opacity <= 0)
    return {};
  auto sampling = path;
  sampling.brush.spacing = 25;
  sampling.brush.pressure_size = false;
  std::vector<ribbon_detail::Field> fields;
  std::vector<PaintPixels::SurfaceTriangle> triangles;
  std::set<std::array<double, 15>> seen;
  auto copies = symmetry_transforms(path.symmetry);
  copies.insert(copies.begin(), SymmetryTransform{});
  std::size_t contacts = 0;
  for (const auto &copy : copies) {
    auto transformed = sampling;
    for (auto &point : transformed.points) {
      point.position = copy.point(point.position);
      point.normal = copy.vector(point.normal);
      if (point.custom) {
        point.tangent_in = copy.vector(point.tangent_in);
        point.tangent_out = copy.vector(point.tangent_out);
      }
    }
    auto projection = path.brush.projection;
    projection.alignment = BrushAlignment::TangentWrap;
    projection.orientation = copy.orientation;
    projection.symmetry_ray = copy.orientation < 0;
    auto dense = dense_surface_path(
        transformed, extent, [&](const SurfacePathPoint &p) -> std::optional<SurfacePathPoint> {
          const auto hit = contact(p, 100, false, projection);
          if (!hit)
            return {};
          auto result = p;
          for (int c = 0; c < 3; ++c)
            result.position[c] = hit->position[c];
          result.uv = hit->footprint.center;
          return result;
        });
    auto definition = transformed;
    definition.brush = path.brush;
    fields.push_back(ribbon_detail::field(definition, extent, dense));
    for (const auto &sample : dense) {
      ++contacts;
      const float diameter = std::clamp(
          path.brush.diameter_at(sample.point.pressure) * 4, .1f, 100.f);
      const auto hit = contact(sample.point, diameter, true, projection);
      if (!hit || !hit->footprint.surface)
        continue;
      for (const auto &tri : hit->footprint.surface->triangles) {
        std::array<double, 15> key{};
        int n = 0;
        for (int i = 0; i < 3; ++i) {
          for (double v : tri.position[i])
            key[n++] = v;
          for (double v : tri.uv[i])
            key[n++] = v;
        }
        if (seen.insert(key).second) {
          triangles.push_back(tri);
        }
      }
    }
  }
  if (record_source)
    return record_ribbon_path(path,
                              std::move(fields),
                              std::move(triangles),
                              resolution,
                              user_resolutions,
                              tiles,
                              std::move(mask),
                              displacement_resolution);
  SurfacePathInk output;
  std::map<int, ribbon_detail::Raster> cache;
  const auto render = [&](int size) -> const ribbon_detail::Raster & {
    auto found = cache.find(size);
    if (found == cache.end())
      found = cache
                  .emplace(size, ribbon_detail::raster(path, fields, triangles, size, tiles, mask))
                  .first;
    return found->second;
  };
  output.primary.paint = ribbon_detail::pixels(render(resolution), resolution, path, 0);
  if (path.brush.displacement)
    output.displacement = ribbon_detail::pixels(
        render(displacement_resolution ? displacement_resolution : resolution),
        displacement_resolution ? displacement_resolution : resolution,
        path,
        3);
  if (path.brush.ao)
    output.primary.ao = ribbon_detail::pixels(render(resolution), resolution, path, 1);
  for (int slot = 0; slot < 8; ++slot)
    if (path.brush.users[slot])
      output.users[slot] = ribbon_detail::pixels(
          render(user_resolutions[slot]), user_resolutions[slot], path, 2, slot);
  return output;
}
}  // namespace bts
