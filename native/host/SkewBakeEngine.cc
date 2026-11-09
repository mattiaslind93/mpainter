#include "TextureExtent.hh"
#include "SkewBakeEngine.hh"
#include "TextureMemory.hh"
#include "MeshBakeFilter.hh"
#include "BakeMatch.hh"
#include <QJsonDocument>
namespace bts {
struct SkewBakeEngine::Impl {
  std::shared_ptr<const SceneSnapshot> low, high;
  std::shared_ptr<const SkewGeometry> edges;
  struct Part {
    std::shared_ptr<const SceneSnapshot> low, high;
    std::unique_ptr<TexturePadding> padding;
  };
  struct Set {
    std::vector<Part> parts;
    std::map<int, std::vector<float>> raw;
    int width = 0, height = 0;
    bool matched = false;
    std::string matching;
    std::map<int, MeshMapPlane> filtered;
  };
  std::map<std::uint32_t, Set> sets;
  Impl(std::shared_ptr<const SceneSnapshot> a, std::shared_ptr<const SceneSnapshot> b)
      : low(std::move(a)), high(std::move(b)), edges(std::make_shared<SkewGeometry>(*low))
  {
  }
  void prepare(Set &set, std::uint32_t material, const SkewBakeOptions &options)
  {
    const std::string matching = options.ray.match_names ?
                                     options.ray.low_marker + "\n" + options.ray.high_marker :
                                     "*";
    if (!set.parts.empty() && set.matching == matching)
      return;
    set.parts.clear();
    set.raw.clear();
    set.filtered.clear();
    set.matched = options.ray.match_names;
    set.matching = matching;
    if (!set.matched) {
      set.parts.push_back({low, high, std::make_unique<TexturePadding>(*low, material)});
      return;
    }
    std::set<std::uint32_t> objects;
    for (const auto &t : low->uv_triangles)
      if (low->materials.at(t.material).session_id == material)
        objects.insert(std::uint32_t(t.vertex_ids[0] >> 32));
    std::uint64_t cached_triangles = 0;
    for (auto uid : objects) {
      auto lp = std::make_shared<SceneSnapshot>();
      lp->materials = low->materials;
      lp->object_names = low->object_names;
      lp->minimum = low->minimum;
      lp->maximum = low->maximum;
      lp->has_bounds = low->has_bounds;
      for (const auto &t : low->uv_triangles)
        if (std::uint32_t(t.vertex_ids[0] >> 32) == uid)
          lp->uv_triangles.push_back(t);
      auto hp = std::make_shared<SceneSnapshot>();
      hp->materials = high->materials;
      hp->object_names = high->object_names;
      hp->minimum = high->minimum;
      hp->maximum = high->maximum;
      hp->has_bounds = high->has_bounds;
      std::set<std::uint32_t> matches;
      const auto name = low->object_names.find(uid);
      if (name == low->object_names.end())
        continue;
      for (const auto &[id, label] : high->object_names)
        if (bake_names_match(name->second, label, options.ray.low_marker, options.ray.high_marker))
          matches.insert(id);
      for (const auto &t : high->uv_triangles)
        if (matches.contains(std::uint32_t(t.vertex_ids[0] >> 32)))
          hp->uv_triangles.push_back(t);
      if (hp->uv_triangles.empty())
        continue;
      cached_triangles += hp->uv_triangles.size();
      set.parts.push_back({lp, hp, std::make_unique<TexturePadding>(*lp, material)});
    }
  }
};
SkewBakeEngine::SkewBakeEngine(std::shared_ptr<const SceneSnapshot> low,
                               std::shared_ptr<const SceneSnapshot> high)
    : impl_(std::make_unique<Impl>(std::move(low), std::move(high)))
{
}
SkewBakeEngine::~SkewBakeEngine() = default;
std::shared_ptr<const SkewGeometry> SkewBakeEngine::geometry() const
{
  return impl_->edges;
}
MeshMap SkewBakeEngine::bake(std::uint32_t material,
                             const SkewField &field,
                             const std::set<int> &tiles,
                             const SkewBakeOptions &options,
                             const std::map<int, PaintPixels::Bounds> &regions,
                             const std::atomic<bool> &cancel)
{
  if (cancel.load())
    return {};
  require_texture_resolution(options.resolution, "Skew bake width", 32, true);
  require_texture_resolution(options.height, "Skew bake height", 32, true);
  if (!valid_sparse_texture_extent(options.resolution, 32, true) ||
      !valid_sparse_texture_extent(options.height, 32, true) || options.sampling < 1 ||
      options.sampling > 8 || (options.sampling & (options.sampling - 1)) ||
      options.dilation < 0 || options.dilation > 64 || options.ray.use_cage ||
      !options.ray.average_normals)
    throw std::invalid_argument("Invalid automatic skew bake options");
  auto &s = *impl_;
  auto &cache = s.sets[material];
  s.prepare(cache, material, options);
  const int width = options.resolution * options.sampling,
            height = options.height * options.sampling;
  const auto texels = std::uint64_t(width) * height;
  if (cache.width != width || cache.height != height) {
    cache.raw.clear();
    cache.filtered.clear();
    cache.width = width;
    cache.height = height;
  }
  MeshMap output;
  output.name = "Normal";
  for (int tile : tiles) {
    if (cancel.load())
      return {};
    auto &raw = cache.raw[tile];
    const bool complete = raw.size() != texels * 4 || regions.empty();
    if (raw.size() != texels * 4) {
      std::uint64_t held = 0;
      for (const auto &[id, set] : s.sets)
        for (const auto &[number, pixels] : set.raw)
          held += pixels.size() * sizeof(float);
      raw.assign(texels * 4, 0);
    }
    auto area = complete               ? PaintPixels::Bounds{0, 0, width, height} :
                regions.contains(tile) ? regions.at(tile) :
                                         PaintPixels::Bounds{};
    if (!complete && !area.empty()) {
      area.x0 *= options.sampling;
      area.x1 *= options.sampling;
      area.y0 *= options.sampling;
      area.y1 *= options.sampling;
    }
    if (area.empty() && cache.filtered.contains(tile)) {
      output.tiles.emplace(tile, cache.filtered.at(tile));
      continue;
    }
    if (!area.empty()) {
      area.x0 = std::clamp(area.x0, 0, width);
      area.x1 = std::clamp(area.x1, 0, width);
      area.y0 = std::clamp(area.y0, 0, height);
      area.y1 = std::clamp(area.y1, 0, height);
      for (int y = area.y0; y < area.y1; ++y)
        std::fill(raw.begin() + (std::size_t(y) * width + area.x0) * 4,
                  raw.begin() + (std::size_t(y) * width + area.x1) * 4,
                  0);
      for (auto &part : cache.parts) {
        if (cancel.load())
          return {};
        auto ray = options.ray;
        ray.cache_source = true;
        ray.regions = {{tile, area}};
        ray.skew_weight_at = [&](int t,
                                 double u,
                                 double v,
                                 const std::array<float, 3> &point,
                                 std::uint32_t object) {
          return s.edges->weight(field, t, u, v, point, object);
        };
        const auto atlas = part.padding->project_source(
            *s.low, *part.high, nullptr, ray, width, height, {tile}, false);
        if (!atlas)
          continue;
        const auto &pixels = atlas->tiles.at(tile);
        for (int y = area.y0; y < area.y1; ++y)
          for (int x = area.x0; x < area.x1; ++x) {
            const auto index = std::size_t(y) * width + x;
            if (!pixels[index].valid)
              continue;
            for (int k = 0; k < 3; ++k)
              raw[index * 4 + k] = pixels[index].baked_normal[k];
            raw[index * 4 + 3] = 1;
          }
      }
    }
    if (cancel.load())
      return {};
    auto filtered = filter_mesh_bake(raw,
                                     width,
                                     height,
                                     options.resolution,
                                     options.height,
                                     options.dilation,
                                     options.diffusion,
                                     0);
    auto data = std::make_shared<MeshMapPixels>(
        options.resolution, std::move(filtered), options.height);
    cache.filtered[tile] = MeshMapPlane{options.resolution, data->hash, data, options.height};
    output.tiles.emplace(tile, cache.filtered.at(tile));
  }
  return output;
}
}  // namespace bts
