#include "TextureExtent.hh"
#include "AutoBakeEngine.hh"
#include "TextureMemory.hh"
#include "BakeMatch.hh"
#include <chrono>
#include <algorithm>
namespace bts {
struct AutoBakeEngine::Impl {
  std::shared_ptr<const SceneSnapshot> low, high, cage;
  std::shared_ptr<const SkewGeometry> edges;
  struct Part {
    std::shared_ptr<const SceneSnapshot> low, high;
    std::unique_ptr<TexturePadding> geometry;
  };
  struct Set {
    std::string key;
    std::vector<Part> parts;
  };
  std::map<std::uint32_t, Set> sets;
  Impl(std::shared_ptr<const SceneSnapshot> l,
       std::shared_ptr<const SceneSnapshot> h,
       std::shared_ptr<const SceneSnapshot> c)
      : low(std::move(l)), high(std::move(h)), cage(std::move(c))
  {
    if (!low || low->uv_triangles.empty())
      throw std::invalid_argument("Automatic bake has no project mesh");
    edges = std::make_shared<SkewGeometry>(*low);
  }
  Set &prepare(std::uint32_t material, const AutoBakeOptions &o)
  {
    auto &set = sets[material];
    const auto key = std::to_string(o.maps.use_project_mesh) + ":" +
                     std::to_string(o.ray.average_normals) + ":" + std::to_string(o.ray.use_cage) +
                     ":" + (o.ray.match_names ? o.ray.low_marker + "\n" + o.ray.high_marker : "*");
    if (set.key == key && !set.parts.empty())
      return set;
    set.parts.clear();
    set.key = key;
    if (o.maps.use_project_mesh || !o.ray.match_names) {
      set.parts.push_back({low, high, std::make_unique<TexturePadding>(*low, material)});
      return set;
    }
    std::set<std::uint32_t> objects;
    for (const auto &t : low->uv_triangles)
      if (low->materials.at(t.material).session_id == material)
        objects.insert(std::uint32_t(t.vertex_ids[0] >> 32));
    std::uint64_t held = 0;
    for (auto uid : objects) {
      const auto found = low->object_names.find(uid);
      if (found == low->object_names.end())
        continue;
      auto lp = std::make_shared<SceneSnapshot>(), hp = std::make_shared<SceneSnapshot>();
      lp->materials = low->materials;
      lp->minimum = low->minimum;
      lp->maximum = low->maximum;
      lp->has_bounds = low->has_bounds;
      hp->object_names = high->object_names;
      hp->materials = high->materials;
      hp->minimum = high->minimum;
      hp->maximum = high->maximum;
      hp->has_bounds = high->has_bounds;
      for (const auto &t : low->uv_triangles)
        if (std::uint32_t(t.vertex_ids[0] >> 32) == uid)
          lp->uv_triangles.push_back(t);
      std::set<std::uint32_t> matches;
      for (const auto &[id, name] : high->object_names)
        if (bake_names_match(found->second, name, o.ray.low_marker, o.ray.high_marker))
          matches.insert(id);
      for (const auto &t : high->uv_triangles)
        if (matches.contains(std::uint32_t(t.vertex_ids[0] >> 32)))
          hp->uv_triangles.push_back(t);
      if (hp->uv_triangles.empty())
        continue;
      held += (lp->uv_triangles.size() + hp->uv_triangles.size()) * sizeof(UvFaceTriangle);

      set.parts.push_back({lp, hp, std::make_unique<TexturePadding>(*lp, material)});
    }
    return set;
  }
};
AutoBakeEngine::AutoBakeEngine(std::shared_ptr<const SceneSnapshot> l,
                               std::shared_ptr<const SceneSnapshot> h,
                               std::shared_ptr<const SceneSnapshot> c)
    : impl_(std::make_unique<Impl>(std::move(l), std::move(h), std::move(c)))
{
}
AutoBakeEngine::~AutoBakeEngine() = default;
std::shared_ptr<const SkewGeometry> AutoBakeEngine::geometry() const
{
  return impl_->edges;
}
MeshMaps AutoBakeEngine::bake(std::uint32_t material,
                              const std::string &name,
                              const MaterialValues &base,
                              const std::set<int> &tiles,
                              const SkewField &field,
                              const AutoBakeOptions &options,
                              const std::atomic<bool> &cancel)
{
  if (cancel.load())
    return {};
  if (tiles.size() > 1) {
    MeshMaps result;
    for (int tile : tiles) {
      auto part = bake(material, name, base, {tile}, field, options, cancel);
      if (cancel.load())
        return {};
      for (int role = 0; role < 7; ++role)
        if (part[role]) {
          if (!result[role])
            result[role] = MeshMap{};
          result[role]->name = part[role]->name;
          result[role]->tiles.merge(part[role]->tiles);
        }
    }
    return result;
  }
  auto &s = *impl_;
  auto settings = options.maps;
  require_texture_resolution(settings.resolution, "Bake width", 32, true);
  require_texture_resolution(settings.rows(), "Bake height", 32, true);
  if (!valid_sparse_texture_extent(settings.resolution, 32, true) ||
      !valid_sparse_texture_extent(settings.rows(), 32, true) ||
      (settings.resolution & (settings.resolution - 1)) ||
      (settings.rows() & (settings.rows() - 1)) ||
      (settings.sampling != 1 && settings.sampling != 2 && settings.sampling != 4 &&
       settings.sampling != 8) ||
      settings.rays < 1 || settings.rays > 512 || settings.dilation < 0 ||
      settings.dilation > 64 || tiles.empty())
    throw std::invalid_argument("Invalid automatic bake parameters");
  const int width = settings.resolution * settings.sampling,
            height = settings.rows() * settings.sampling;
  const auto count = std::uint64_t(width) * height;

  if (std::none_of(
          settings.enabled.begin(), settings.enabled.end(), [](bool enabled) { return enabled; }))
    throw std::length_error("Automatic bake maps are empty");
  // MeshBakeJob admits only its current working tile. Completed outputs are
  // immutable disk snapshots, so their aggregate size is not a RAM/VRAM bill.
  if (!settings.use_project_mesh && (!s.high || s.high->uv_triangles.empty()))
    throw std::invalid_argument("Choose a high-poly source");
  if (options.ray.use_cage) {
    if (!s.cage)
      throw std::invalid_argument("Choose a cage file");
    TexturePadding::validate_cage(*s.low, *s.cage);
  }
  settings.external_source = !settings.use_project_mesh || options.ray.use_cage;
  const auto source = settings.use_project_mesh ? s.low : s.high;
  settings.ignore_backface_objects = bake_backface_objects(
      *source, options.ray.ignore_backface, options.ray.ignore_marker);
  if (options.ray.match_names && settings.use_project_mesh)
    for (const auto &tri : source->uv_triangles) {
      const auto object = std::uint32_t(tri.vertex_ids[0] >> 32);
      settings.matched_objects[object].insert(object);
    }
  auto &set = s.prepare(material, options);
  if (set.parts.size() == 1 && settings.use_project_mesh && !options.ray.use_cage) {
    TexturePadding geometry(*s.low, material);
    auto atlas = geometry.projection_geometry(width, tiles, false, height);
    MeshBakeJob job(source, {{material, name, base, std::move(atlas)}}, settings);
    while (!job.finished.load(std::memory_order_acquire)) {
      job.process_gpu();
      if (cancel.load())
        job.cancel();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (cancel.load())
      return {};
    if (!job.error.empty())
      throw std::runtime_error(job.error);
    return job.result.empty() ? MeshMaps{} : std::move(job.result.front().maps);
  }
  auto merged = std::make_shared<FillSurfaceAtlas>();
  merged->resolution = width;
  merged->height = height;
  for (int axis = 0; axis < 3; ++axis)
    merged->extent = std::max(merged->extent, double(s.low->maximum[axis] - s.low->minimum[axis]));
  for (int tile : tiles) {
    merged->tiles[tile].resize(count);
    if (settings.enabled[4])
      merged->curvature[tile].resize(count);
  }
  for (auto &part : set.parts) {
    if (cancel.load())
      return {};
    auto ray = options.ray;
    ray.cache_source = true;
    ray.regions.clear();
    if (!settings.use_project_mesh && !ray.use_cage && ray.average_normals)
      ray.skew_weight_at =
          [&](int tile, double u, double v, const std::array<float, 3> &p, std::uint32_t object) {
            return s.edges->weight(field, tile, u, v, p, object);
          };
    const auto atlas = settings.external_source ?
                           part.geometry->project_source(*s.low,
                                                         settings.use_project_mesh ? *s.low :
                                                                                     *part.high,
                                                         s.cage.get(),
                                                         ray,
                                                         width,
                                                         height,
                                                         tiles,
                                                         false) :
                           part.geometry->projection_geometry(width, tiles, false, height);
    if (!atlas)
      continue;
    std::uint32_t object = 0;
    if (options.ray.match_names && !settings.use_project_mesh && !part.low->uv_triangles.empty()) {
      object = std::uint32_t(part.low->uv_triangles.front().vertex_ids[0] >> 32);
      const auto found = s.low->object_names.find(object);
      if (found != s.low->object_names.end())
        for (const auto &[uid, label] : source->object_names)
          if (bake_names_match(found->second, label, ray.low_marker, ray.high_marker))
            settings.matched_objects[object].insert(uid);
    }
    for (int tile : tiles)
      for (std::size_t i = 0; i < count; ++i)
        if (atlas->tiles.at(tile)[i].valid) {
          auto &point = merged->tiles.at(tile)[i];
          point = atlas->tiles.at(tile)[i];
          point.match_object = object;
          if (settings.enabled[4] && atlas->curvature.contains(tile))
            merged->curvature.at(tile)[i] = atlas->curvature.at(tile)[i];
        }
  }
  if (cancel.load())
    return {};
  MeshBakeJob job(source, {{material, name, base, merged}}, settings);
  while (!job.finished.load(std::memory_order_acquire)) {
    job.process_gpu();
    if (cancel.load())
      job.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (cancel.load())
    return {};
  if (!job.error.empty())
    throw std::runtime_error(job.error);
  return job.result.empty() ? MeshMaps{} : std::move(job.result.front().maps);
}
}  // namespace bts
