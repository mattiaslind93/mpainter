#include "TextureExtent.hh"
#include "ResourceAvailability.hh"
#include <atomic>
#include "TexturePadding.hh"
#include "BakeMatch.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "GpuMarginPlan.hh"
#include "RE_texture_margin.h"
#include <tuple>
#include "FragmentTangents.hh"
#include "SkewCorrection.hh"
#include "SurfaceCurvature.hh"
#include "BLI_kdopbvh.hh"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>
#include "BKE_attribute.hh"
#include "BLI_math_geom.h"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.hh"
#include "BLI_math_vector.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "BLI_math_matrix.h"
#include "IMB_imbuf_types.hh"
#include "MEM_guardedalloc.h"
#include "RE_bake.h"
namespace bts {
using namespace blender;
namespace {
std::atomic<std::uint64_t> padding_invocations{0};
void set_bake_normals(Mesh &mesh, MutableSpan<float3> normals)
{
  const auto fallback = mesh.corner_normals();
  for (int i = 0; i < normals.size(); ++i)
    if (normalize_v3(normals[i]) == 0)
      normals[i] = fallback[i];
  // Welding can collapse a face corner. Legacy short2 fan encoding leaves that
  // corner's space index uninitialized; free normals need no fan lookup.
  auto attribute = mesh.attributes_for_write().lookup_or_add_for_write_only_span<float3>(
      "custom_normal", bke::AttrDomain::Corner);
  if (!attribute)
    throw std::runtime_error("Cannot store bake normals");
  attribute.span.copy_from(normals);
  attribute.finish();
}
}  // namespace
struct TexturePadding::Impl {
  SceneSnapshot source;
  std::uint32_t material_uid = 0;
  Mesh *mesh = nullptr, *cached_high_mesh = nullptr;
  const SceneSnapshot *cached_high_source = nullptr;
  std::map<std::uint32_t, std::string> object_names;
  std::map<int, std::shared_ptr<GeometryOwnershipTile>> ownership;
  std::vector<std::uint64_t> vertex_sources;
  int resolution = 0, height = 0;
  std::map<int, std::vector<char>> masks;
  std::map<int, std::pair<int, int>> mask_sizes;
  std::map<std::tuple<int, int, int, bool>, std::shared_ptr<const GpuMarginPlan>> gpu_margins;
  double extent = 1;
  std::shared_ptr<FillSurfaceAtlas> atlas;
  using BakePixels = ScratchVector<BakePixel>;
  BakePixels rasterize(int tile, int size, int rows = 0)
  {
    if (!rows)
      rows = size;
    if (size < 1 || rows < 1)
      throw std::invalid_argument("Native bake dimensions must be positive");
    resolution = size;
    height = rows;
    if (!mask_sizes.contains(tile) || mask_sizes[tile] != std::pair{size, rows}) {
      masks.erase(tile);
      mask_sizes[tile] = {size, rows};
    }
    const auto count = resource_product(size, rows);
    if (count > std::uint64_t(std::numeric_limits<int>::max()))
      throw std::length_error("Blender BakeTargets pixels_num exceeds signed-int indexing");
    BakePixels pixels(count);
    BakeImage image{};
    image.width = size;
    image.height = rows;
    image.tile_number = tile;
    image.uv_offset[0] = (tile - 1001) % 10;
    image.uv_offset[1] = (tile - 1001) / 10;
    Image *material_image = nullptr;
    BakeTargets targets{};
    targets.images = &image;
    targets.images_num = 1;
    targets.material_to_image = &material_image;
    targets.materials_num = 1;
    targets.pixels_num = int(count);
    RE_bake_pixels_populate(mesh, pixels.data(), count, &targets, "UVMap");
    if (!masks.contains(tile)) {
      std::vector<char> mask(count, 0);
      RE_bake_mask_fill(pixels.data(), count, mask.data());
      masks.emplace(tile, std::move(mask));
    }
    return pixels;
  }
  ~Impl()
  {
    if (mesh)
      BKE_id_free(nullptr, mesh);
    if (cached_high_mesh)
      BKE_id_free(nullptr, cached_high_mesh);
  }
};
TexturePadding::TexturePadding(const SceneSnapshot &snapshot, std::uint32_t uid)
    : impl_(std::make_unique<Impl>())
{
  impl_->source.materials = snapshot.materials;
  impl_->source.object_names = snapshot.object_names;
  impl_->source.minimum = snapshot.minimum;
  impl_->source.maximum = snapshot.maximum;
  impl_->material_uid = uid;
  impl_->extent = 0;
  for (int i = 0; i < 3; ++i)
    impl_->extent = std::max(impl_->extent, double(snapshot.maximum[i] - snapshot.minimum[i]));
  if (impl_->extent < 1e-6)
    impl_->extent = 1;
  std::size_t material = snapshot.materials.size();
  for (std::size_t i = 0; i < snapshot.materials.size(); ++i)
    if (snapshot.materials[i].session_id == uid)
      material = i;
  if (material == snapshot.materials.size())
    throw std::runtime_error("Padding Texture Set material is missing");
  std::vector<const UvFaceTriangle *> faces;
  std::map<std::uint64_t, int> vertices;
  std::vector<float3> positions;
  for (const auto &triangle : snapshot.uv_triangles)
    if (triangle.material == material) {
      impl_->source.uv_triangles.push_back(triangle);
      faces.push_back(&triangle);
      const auto uid = std::uint32_t(triangle.triangle_id >> 32);
      if (const auto found = snapshot.object_names.find(uid); found != snapshot.object_names.end())
        impl_->object_names[uid] = found->second;
      for (int i = 0; i < 3; ++i)
        if (!vertices.contains(triangle.vertex_ids[i])) {
          vertices.emplace(triangle.vertex_ids[i], int(positions.size()));
          positions.emplace_back(triangle.positions[i].data());
        }
    }
  if (faces.empty())
    return;
  impl_->vertex_sources.resize(positions.size());
  for (const auto &[source, index] : vertices)
    impl_->vertex_sources[index] = source;
  auto *mesh = impl_->mesh = BKE_mesh_new_nomain(
      int(positions.size()), 0, int(faces.size()), int(faces.size() * 3));
  if (!mesh)
    throw std::runtime_error("Cannot allocate native padding mesh");
  std::copy(positions.begin(), positions.end(), mesh->vert_positions_for_write().begin());
  auto offsets = mesh->face_offsets_for_write();
  for (int i = 0; i <= int(faces.size()); ++i)
    offsets[i] = i * 3;
  auto corners = mesh->corner_verts_for_write();
  auto attributes = mesh->attributes_for_write();
  auto uv = attributes.lookup_or_add_for_write_only_span<float2>("UVMap", bke::AttrDomain::Corner);
  for (int face = 0; face < int(faces.size()); ++face)
    for (int i = 0; i < 3; ++i) {
      corners[face * 3 + i] = vertices.at(faces[face]->vertex_ids[i]);
      uv.span[face * 3 + i] = float2(faces[face]->corners[i].data());
    }
  uv.finish();
  mesh->tag_topology_changed();
  bke::mesh_calc_edges(*mesh, false, false);
  std::vector<float3> custom_normals(faces.size() * 3, float3(0));
  bool have_normals = false;
  for (std::size_t f = 0; f < faces.size(); ++f)
    if (faces[f]->has_normals) {
      have_normals = true;
      for (int c = 0; c < 3; ++c)
        custom_normals[f * 3 + c] = float3(faces[f]->normals[c].data());
    }
  if (have_normals)
    set_bake_normals(*mesh, MutableSpan<float3>(custom_normals.data(), custom_normals.size()));
}
const std::map<std::uint32_t, std::string> &TexturePadding::mesh_names() const
{
  return impl_->object_names;
}
std::shared_ptr<const GeometryOwnershipTile> TexturePadding::mesh_ownership(int width,
                                                                            int tile,
                                                                            int height)
{
  if (!height)
    height = width;
  if (width < 1 || height < 1)
    throw std::invalid_argument("Invalid geometry ownership dimensions");
  const auto required = std::uint64_t(width) * height * 32, budget = texture_available_ram_bytes();
  if (const auto found = impl_->ownership.find(tile); found != impl_->ownership.end() &&
                                                      found->second->width == width &&
                                                      found->second->height == height)
    return found->second;
  auto atlas = std::make_shared<GeometryOwnershipTile>();
  atlas->width = width;
  atlas->height = height;
  atlas->owners.resize(std::size_t(width) * height, 0);
  if (impl_->mesh) {
    const auto pixels = impl_->rasterize(tile, width, height);
    const auto triangles = impl_->mesh->corner_tris();
    const auto vertices = impl_->mesh->corner_verts();
    for (std::size_t i = 0; i < atlas->owners.size(); ++i) {
      const auto primitive = pixels[i].primitive_id;
      if (primitive >= 0 && primitive < triangles.size())
        atlas->owners[i] = std::uint32_t(
            impl_->vertex_sources[vertices[triangles[primitive][0]]] >> 32);
    }
  }
  impl_->ownership[tile] = atlas;
  return atlas;
}
namespace {
void populate_mesh_curvature(const Mesh &mesh,
                             double extent,
                             const std::set<int> &tiles,
                             FillSurfaceAtlas &atlas)
{
  const auto positions = mesh.vert_positions(), normals = mesh.vert_normals();
  const auto corners = mesh.corner_verts();
  SceneSnapshot source;
  source.minimum.fill(std::numeric_limits<float>::max());
  source.maximum.fill(std::numeric_limits<float>::lowest());
  for (const auto &indices : mesh.corner_tris()) {
    UvFaceTriangle tri;
    tri.has_normals = true;
    for (int k = 0; k < 3; ++k) {
      const int v = corners[indices[k]];
      tri.vertex_ids[k] = v;
      std::copy_n(&positions[v].x, 3, tri.positions[k].begin());
      std::copy_n(&normals[v].x, 3, tri.normals[k].begin());
      for (int a = 0; a < 3; ++a) {
        source.minimum[a] = std::min(source.minimum[a], tri.positions[k][a]);
        source.maximum[a] = std::max(source.maximum[a], tri.positions[k][a]);
      }
    }
    source.uv_triangles.push_back(tri);
  }
  if (source.uv_triangles.empty())
    return;
  SurfaceCurvature field(source, false);
  field.calculate(float(std::max(extent * .03, 1e-8)));
  using Tree = std::unique_ptr<BVHTree, decltype(&BLI_bvhtree_free)>;
  Tree tree(BLI_bvhtree_new(int(source.uv_triangles.size()), 0, 4, 6), BLI_bvhtree_free);
  if (!tree)
    throw std::runtime_error("Cannot build curvature BVH");
  for (int i = 0; i < int(source.uv_triangles.size()); ++i)
    BLI_bvhtree_insert(tree.get(), i, source.uv_triangles[i].positions[0].data(), 3);
  BLI_bvhtree_balance(tree.get());
  const auto nearest = [](void *data, int index, const float p[3], BVHTreeNearest *hit) {
    const auto &tri = static_cast<const SceneSnapshot *>(data)->uv_triangles[index];
    float co[3];
    closest_on_tri_to_point_v3(
        co, p, tri.positions[0].data(), tri.positions[1].data(), tri.positions[2].data());
    const float distance = len_squared_v3v3(co, p);
    if (distance < hit->dist_sq) {
      hit->dist_sq = distance;
      hit->index = index;
      copy_v3_v3(hit->co, co);
    }
  };
  for (int tile : tiles)
    if (!atlas.curvature.contains(tile) && atlas.tiles.contains(tile)) {
      const auto &points = atlas.tiles.at(tile);
      auto &values = atlas.curvature[tile];
      values.resize(points.size());
      threading::parallel_for(IndexRange(points.size()), 256, [&](const IndexRange range) {
        for (std::size_t i : range)
          if (points[i].valid) {
            BVHTreeNearest hit{};
            hit.index = -1;
            hit.dist_sq = std::numeric_limits<float>::max();
            BLI_bvhtree_find_nearest(
                tree.get(), points[i].position.data(), &hit, nearest, &source);
            if (hit.index < 0)
              continue;
            const auto &tri = source.uv_triangles[hit.index];
            float weights[3];
            interp_weights_tri_v3(weights,
                                  tri.positions[0].data(),
                                  tri.positions[1].data(),
                                  tri.positions[2].data(),
                                  hit.co);
            for (int k = 0; k < 3; ++k)
              values[i] += weights[k] * field.vertices[field.corners[hit.index][k]].curvature;
          }
      });
    }
}
}  // namespace
std::shared_ptr<const FillSurfaceAtlas> TexturePadding::projection_geometry(
    int resolution, const std::set<int> &tiles, bool curvature, int height)
{
  if (!height)
    height = resolution;
  if (!impl_->mesh)
    return {};
  if (resolution < 1 || height < 1)
    throw std::length_error("Invalid projection geometry dimensions");
  const auto required = std::uint64_t(resolution) * height *
                        (tiles.size() * sizeof(FillSurfacePoint) + sizeof(BakePixel)),
             budget = texture_available_ram_bytes();
  if (!impl_->atlas) {
    impl_->atlas = std::make_shared<FillSurfaceAtlas>();
    impl_->atlas->resolution = resolution;
    impl_->atlas->height = height;
    impl_->atlas->extent = impl_->extent;
  }
  if (impl_->atlas->resolution != resolution || impl_->atlas->height != height) {
    impl_->atlas = std::make_shared<FillSurfaceAtlas>();
    impl_->atlas->resolution = resolution;
    impl_->atlas->height = height;
    impl_->atlas->extent = impl_->extent;
  }
  for (int c = 0; c < 3; ++c)
    impl_->atlas->center[c] = (impl_->source.minimum[c] + impl_->source.maximum[c]) * .5;
  const auto triangles = impl_->mesh->corner_tris();
  const auto faces = impl_->mesh->corner_tri_faces();
  const auto vertices = impl_->mesh->corner_verts();
  const auto positions = impl_->mesh->vert_positions();
  const auto normals = impl_->mesh->corner_normals();
  for (int tile : tiles)
    if (!impl_->atlas->tiles.contains(tile)) {
      const auto pixels = impl_->rasterize(tile, resolution, height);
      ScratchVector<FillSurfacePoint> points(std::size_t(resolution) * height);
      for (std::size_t i = 0; i < points.size(); ++i) {
        const auto &pixel = pixels[i];
        if (pixel.primitive_id < 0 || pixel.primitive_id >= triangles.size())
          continue;
        const auto corners = triangles[pixel.primitive_id];
        float data[3][3];
        for (int k = 0; k < 3; ++k)
          std::copy_n(&positions[vertices[corners[k]]].x, 3, data[k]);
        auto &point = points[i];
        point.match_object = std::uint32_t(impl_->vertex_sources[vertices[corners[0]]] >> 32);
        interp_barycentric_tri_v3(data, pixel.uv[0], pixel.uv[1], point.position.data());
        for (int k = 0; k < 3; ++k)
          std::copy_n(&normals[corners[k]].x, 3, data[k]);
        interp_barycentric_tri_v3(data, pixel.uv[0], pixel.uv[1], point.normal.data());
        normalize_v3(point.normal.data());
        double dx = 0, dy = 0;
        for (int k = 0; k < 3; ++k)
          std::copy_n(&positions[vertices[corners[k]]].x, 3, data[k]);
        for (int axis = 0; axis < 3; ++axis) {
          const double x = (data[0][axis] - data[2][axis]) * pixel.du_dx +
                           (data[1][axis] - data[2][axis]) * pixel.dv_dx,
                       y = (data[0][axis] - data[2][axis]) * pixel.du_dy +
                           (data[1][axis] - data[2][axis]) * pixel.dv_dy;
          dx += x * x;
          dy += y * y;
        }
        point.footprint = float(std::sqrt(std::max(dx, dy)));
        point.valid = true;
      }
      impl_->atlas->tiles.emplace(tile, std::move(points));
    }
  if (curvature)
    populate_mesh_curvature(*impl_->mesh, impl_->extent, tiles, *impl_->atlas);
  return impl_->atlas;
}
namespace {
using BakeMesh = std::unique_ptr<Mesh, decltype([](Mesh *mesh) {
                                   if (mesh)
                                     BKE_id_free(nullptr, mesh);
                                 })>;
BakeMesh source_mesh(const SceneSnapshot &source)
{
  std::map<std::uint64_t, int> vertices;
  std::vector<float3> positions;
  for (const auto &triangle : source.uv_triangles)
    for (int i = 0; i < 3; ++i)
      if (!vertices.contains(triangle.vertex_ids[i])) {
        vertices.emplace(triangle.vertex_ids[i], int(positions.size()));
        positions.emplace_back(triangle.positions[i].data());
      }
  BakeMesh mesh(BKE_mesh_new_nomain(int(positions.size()),
                                    0,
                                    int(source.uv_triangles.size()),
                                    int(source.uv_triangles.size() * 3)));
  if (!mesh)
    throw std::runtime_error("Cannot allocate native high-poly bake mesh");
  std::copy(positions.begin(), positions.end(), mesh->vert_positions_for_write().begin());
  auto offsets = mesh->face_offsets_for_write();
  for (int i = 0; i <= int(source.uv_triangles.size()); ++i)
    offsets[i] = i * 3;
  auto corners = mesh->corner_verts_for_write();
  for (int face = 0; face < int(source.uv_triangles.size()); ++face)
    for (int i = 0; i < 3; ++i)
      corners[face * 3 + i] = vertices.at(source.uv_triangles[face].vertex_ids[i]);
  mesh->tag_topology_changed();
  bke::mesh_calc_edges(*mesh, false, false);
  std::vector<float3> normals(source.uv_triangles.size() * 3, float3(0));
  bool have = false;
  for (std::size_t f = 0; f < source.uv_triangles.size(); ++f)
    if (source.uv_triangles[f].has_normals) {
      have = true;
      for (int c = 0; c < 3; ++c)
        normals[f * 3 + c] = float3(source.uv_triangles[f].normals[c].data());
    }
  if (have)
    set_bake_normals(*mesh, MutableSpan<float3>(normals.data(), normals.size()));
  return mesh;
}
using ObjectVertices = std::map<std::uint32_t, std::map<std::uint32_t, std::array<float, 3>>>;
ObjectVertices object_vertices(const SceneSnapshot &scene)
{
  ObjectVertices objects;
  for (const auto &triangle : scene.uv_triangles)
    for (int i = 0; i < 3; ++i)
      objects[std::uint32_t(triangle.vertex_ids[i] >> 32)][std::uint32_t(triangle.vertex_ids[i])] =
          triangle.positions[i];
  return objects;
}
std::map<std::uint64_t, std::array<float, 3>> cage_positions(const SceneSnapshot &low,
                                                             const SceneSnapshot &cage)
{
  const auto low_objects = object_vertices(low), cage_objects = object_vertices(cage);
  if (low_objects.size() != cage_objects.size())
    throw std::invalid_argument(
        "Cage must have the same mesh-object and vertex counts as the project");
  std::map<std::uint64_t, std::array<float, 3>> result;
  auto other = cage_objects.begin();
  for (const auto &[object, vertices] : low_objects) {
    const auto cage_object = other->first;
    const auto &replacement = (other++)->second;
    if (low.object_vertex_counts.contains(object) &&
        cage.object_vertex_counts.contains(cage_object) &&
        low.object_vertex_counts.at(object) != cage.object_vertex_counts.at(cage_object))
      throw std::invalid_argument("Cage vertex count does not match the project");
    if (vertices.size() != replacement.size())
      throw std::invalid_argument("Cage vertex count does not match the project");
    for (const auto &[index, position] : vertices) {
      const auto found = replacement.find(index);
      if (found == replacement.end())
        throw std::invalid_argument("Cage vertex indices do not match the project");
      result[(std::uint64_t(object) << 32) | index] = found->second;
    }
  }
  return result;
}
}  // namespace
void TexturePadding::validate_cage(const SceneSnapshot &low, const SceneSnapshot &cage)
{
  cage_positions(low, cage);
}
std::shared_ptr<const FillSurfaceAtlas> TexturePadding::project_source(
    const SceneSnapshot &low_scene,
    const SceneSnapshot &high,
    const SceneSnapshot *cage,
    const BakeRayOptions &options,
    int width,
    int height,
    const std::set<int> &tiles,
    bool curvature,
    UvTransferMap *transfer)
{
  if (high.uv_triangles.empty())
    throw std::invalid_argument("High-definition mesh has no usable triangles");
  if (options.frontal < 0 || options.rear < 0 || !std::isfinite(options.frontal) ||
      !std::isfinite(options.rear))
    throw std::invalid_argument("Invalid bake ray distance");
  if ((options.skew_weight || options.skew_weight_at) &&
      (options.use_cage || !options.average_normals))
    throw std::invalid_argument(
        "Skew correction requires distance-based cage and averaged normals");
  impl_->atlas.reset();
  projection_geometry(width, tiles, false, height);
  if (!impl_->mesh)
    return {};
  if (!options.cache_source || impl_->cached_high_source != &high) {
    if (impl_->cached_high_mesh)
      BKE_id_free(nullptr, impl_->cached_high_mesh);
    impl_->cached_high_mesh = nullptr;
    impl_->cached_high_source = nullptr;
    impl_->cached_high_mesh = source_mesh(high).release();
    impl_->cached_high_source = &high;
  }
  auto *high_mesh = impl_->cached_high_mesh;
  BakeMesh cage_mesh;
  if (options.use_cage) {
    if (!cage)
      throw std::invalid_argument("Choose a custom cage OBJ before baking");
    const auto replacement = cage_positions(low_scene, *cage);
    cage_mesh.reset(BKE_mesh_copy_for_eval(*impl_->mesh));
    auto positions = cage_mesh->vert_positions_for_write();
    for (int index = 0; index < int(impl_->vertex_sources.size()); ++index)
      positions[index] = float3(replacement.at(impl_->vertex_sources[index]).data());
    cage_mesh->tag_positions_changed();
  }
  else if (options.average_normals)
    cage_mesh.reset(BKE_mesh_copy_for_eval(*impl_->mesh));
  else {
    auto sharp = impl_->mesh->attributes_for_write().lookup_or_add_for_write_span<bool>(
        "sharp_face", bke::AttrDomain::Face);
    sharp.span.fill(true);
    sharp.finish();
    impl_->mesh->tag_sharpness_changed();
  }
  const double scale = options.relative ? impl_->extent : 1, front = options.frontal * scale,
               rear = options.rear * scale;
  if (!options.use_cage && front + rear <= 0)
    throw std::invalid_argument("Bake ray distances must span a nonzero interval");
  Object dummy{};
  std::copy_n("OBBake source", 14, dummy.id.name);
  BakeHighPolyData source{};
  source.ob = source.ob_eval = &dummy;
  source.mesh = high_mesh;
  unit_m4(source.obmat);
  unit_m4(source.imat);
  float identity[4][4];
  unit_m4(identity);
  const auto high_triangles = high_mesh->corner_tris();
  const auto high_faces = high_mesh->corner_tri_faces();
  const auto high_vertices = high_mesh->corner_verts();
  const auto high_positions = high_mesh->vert_positions();
  const auto high_normals = high_mesh->face_normals();
  const auto low_triangles = impl_->mesh->corner_tris();
  const auto low_vertices = impl_->mesh->corner_verts();
  const auto low_normals = impl_->mesh->vert_normals();
  const auto low_corner_normals = impl_->mesh->corner_normals();
  const auto high_corner_normals = high_mesh->corner_normals();
  const auto low_faces = impl_->mesh->corner_tri_faces();
  const auto low_face_normals = impl_->mesh->face_normals();
  std::vector<TangentFrame> high_frames, low_frames;
  if (transfer || options.per_fragment) {
    if (transfer && width != height)
      throw std::invalid_argument("Stroke reprojection requires square tiles");
    if (transfer)
      transfer->resolution = width;
    for (const auto &triangle : high.uv_triangles)
      high_frames.push_back(uv_tangent_frame(triangle));
    const auto positions = impl_->mesh->vert_positions();
    const auto uv = impl_->mesh->attributes().lookup<float2>("UVMap", bke::AttrDomain::Corner);
    for (const auto &corners : low_triangles) {
      UvFaceTriangle triangle;
      for (int k = 0; k < 3; ++k) {
        std::copy_n(&positions[low_vertices[corners[k]]].x, 3, triangle.positions[k].begin());
        const auto value = uv.varray[corners[k]];
        triangle.corners[k] = {value.x, value.y};
      }
      low_frames.push_back(uv_tangent_frame(triangle));
    }
  }
  for (int tile : tiles) {
    if (transfer)
      transfer->tiles[tile].resize(std::size_t(width) * height);
    auto low = impl_->rasterize(tile, width, height);
    const std::size_t count = std::size_t(width) * height;
    Impl::BakePixels projected(count);
    for (std::size_t i = 0; i < count; ++i) {
      projected[i] = {};
      projected[i].primitive_id = -1;
    }
    ScratchVector<std::array<float, 3>> projection_normals;
    if (const auto region = options.regions.find(tile); region != options.regions.end()) {
      const auto &r = region->second;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
          if (x < r.x0 || x >= r.x1 || y < r.y0 || y >= r.y1)
            low[std::size_t(y) * width + x].primitive_id = -1;
    }
    if (options.skew_weight || options.skew_weight_at) {
      projection_normals.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        const int primitive = low[i].primitive_id;
        if (primitive < 0)
          continue;
        const double u = (i % width + .5) / width, v = (i / width + .5) / height;
        float amount = 0;
        if (options.skew_weight_at) {
          const auto corners = low_triangles[primitive];
          const auto positions = impl_->mesh->vert_positions();
          float points[3][3];
          for (int k = 0; k < 3; ++k)
            std::copy_n(&positions[low_vertices[corners[k]]].x, 3, points[k]);
          std::array<float, 3> point;
          interp_barycentric_tri_v3(points, low[i].uv[0], low[i].uv[1], point.data());
          const auto object = std::uint32_t(
              impl_->source.uv_triangles[low_faces[primitive]].vertex_ids[0] >> 32);
          amount = options.skew_weight_at(tile, u, v, point, object);
        }
        else
          amount = options.skew_weight(tile, u, v);
        if (!std::isfinite(amount) || amount <= 0)
          continue;
        const auto corners = low_triangles[primitive];
        float normals[3][3], average[3];
        for (int k = 0; k < 3; ++k)
          std::copy_n(&low_normals[low_vertices[corners[k]]].x, 3, normals[k]);
        interp_barycentric_tri_v3(normals, low[i].uv[0], low[i].uv[1], average);
        const auto face = low_face_normals[low_faces[primitive]];
        projection_normals[i] = skew_projection_normal(
            {average[0], average[1], average[2]}, {face.x, face.y, face.z}, amount);
      }
    }
    static_assert(sizeof(std::array<float, 3>) == 3 * sizeof(float));
    const auto *override_normals = projection_normals.empty() ?
                                       nullptr :
                                       reinterpret_cast<const float (*)[3]>(
                                           projection_normals.data());
    if (!RE_bake_pixels_populate_from_objects(impl_->mesh,
                                              low.data(),
                                              projected.data(),
                                              &source,
                                              1,
                                              count,
                                              options.use_cage,
                                              float(front),
                                              options.use_cage ? 0.f : float(front + rear),
                                              identity,
                                              identity,
                                              cage_mesh.get(),
                                              override_normals))
      throw std::runtime_error("Native high-poly ray projection failed");
    auto &points = impl_->atlas->tiles.at(tile);
    ScratchVector<float> tangent_normals(count * 3, 0);
    for (std::size_t i = 0; i < count; ++i) {
      auto &point = points[i];
      if (!point.valid || low[i].primitive_id < 0 || projected[i].primitive_id < 0) {
        point.valid = false;
        continue;
      }
      const auto &hit = projected[i];
      const auto corners = high_triangles[hit.primitive_id];
      const auto normal = high_normals[high_faces[hit.primitive_id]];
      float direction[3];
      if (options.use_cage) {
        float positions[3][3], origin[3];
        const auto low_corners = low_triangles[low[i].primitive_id];
        for (int k = 0; k < 3; ++k)
          std::copy_n(
              &cage_mesh->vert_positions()[low_vertices[low_corners[k]]].x, 3, positions[k]);
        interp_barycentric_tri_v3(positions, low[i].uv[0], low[i].uv[1], origin);
        sub_v3_v3v3(direction, point.position.data(), origin);
      }
      else if (override_normals && !is_zero_v3(override_normals[i])) {
        copy_v3_v3(direction, override_normals[i]);
        negate_v3(direction);
      }
      else if (options.average_normals) {
        float normals[3][3];
        const auto low_corners = low_triangles[low[i].primitive_id];
        for (int k = 0; k < 3; ++k)
          std::copy_n(&low_normals[low_vertices[low_corners[k]]].x, 3, normals[k]);
        interp_barycentric_tri_v3(normals, low[i].uv[0], low[i].uv[1], direction);
        negate_v3(direction);
      }
      else {
        copy_v3_v3(direction, low_face_normals[low_faces[low[i].primitive_id]]);
        negate_v3(direction);
      }
      if (bake_filters_backface(
              high,
              std::uint32_t(high.uv_triangles[hit.primitive_id].vertex_ids[0] >> 32),
              options.ignore_backface,
              options.ignore_marker) &&
          dot_v3v3(direction, normal) >= 0)
      {
        point.valid = false;
        low[i].primitive_id = -1;
        continue;
      }
      if (transfer) {
        auto &sample = transfer->tiles.at(tile)[i];
        const auto &triangle = high.uv_triangles[hit.primitive_id];
        for (int axis = 0; axis < 2; ++axis)
          sample.source[axis] = hit.uv[0] * triangle.corners[0][axis] +
                                hit.uv[1] * triangle.corners[1][axis] +
                                (1 - hit.uv[0] - hit.uv[1]) * triangle.corners[2][axis];
        sample.normal = uv_normal_transform(high_frames.at(hit.primitive_id),
                                            low_frames.at(low[i].primitive_id));
        sample.valid = true;
      }
      float positions[3][3];
      for (int k = 0; k < 3; ++k)
        std::copy_n(&high_positions[high_vertices[corners[k]]].x, 3, positions[k]);
      interp_barycentric_tri_v3(positions, hit.uv[0], hit.uv[1], point.position.data());
      for (int k = 0; k < 3; ++k)
        std::copy_n(&high_corner_normals[corners[k]].x, 3, positions[k]);
      interp_barycentric_tri_v3(positions, hit.uv[0], hit.uv[1], point.normal.data());
      normalize_v3(point.normal.data());
      for (int c = 0; c < 3; ++c)
        tangent_normals[i * 3 + c] =
            point.normal[c];  // Native tangent conversion compresses signed world vectors itself.
      const auto material = high.uv_triangles[hit.primitive_id].material;
      std::copy_n(
          high.materials.at(material).values.base_color.begin(), 3, point.source_color.begin());
    }
    const eBakeNormalSwizzle swizzle[3] = {
        R_BAKE_POSX, options.directx ? R_BAKE_NEGY : R_BAKE_POSY, R_BAKE_POSZ};
    if (options.per_fragment) {
      for (std::size_t i = 0; i < count; ++i)
        if (points[i].valid) {
          const auto primitive = low[i].primitive_id;
          const auto corners = low_triangles[primitive];
          float normals[3][3], normal[3];
          for (int k = 0; k < 3; ++k)
            std::copy_n(&low_corner_normals[corners[k]].x, 3, normals[k]);
          interp_barycentric_tri_v3(normals, low[i].uv[0], low[i].uv[1], normal);
          const auto frame = fragment_tangent_frame(low_frames.at(primitive),
                                                    {normal[0], normal[1], normal[2]});
          auto value = fragment_world_to_tangent(
              frame,
              {tangent_normals[i * 3], tangent_normals[i * 3 + 1], tangent_normals[i * 3 + 2]});
          normalize_v3(value.data());
          if (options.directx)
            value[1] = -value[1];
          for (int c = 0; c < 3; ++c)
            tangent_normals[i * 3 + c] = value[c] * .5f + .5f;
        }
    }
    else
      RE_bake_normal_world_to_tangent(
          low.data(), count, 3, tangent_normals.data(), impl_->mesh, swizzle, identity);
    for (std::size_t i = 0; i < count; ++i)
      if (points[i].valid)
        std::copy_n(tangent_normals.data() + i * 3, 3, points[i].baked_normal.begin());
  }
  if (curvature)
    populate_mesh_curvature(*high_mesh, impl_->extent, tiles, *impl_->atlas);
  return impl_->atlas;
}

TexturePadding::~TexturePadding() = default;
std::shared_ptr<TexturePadding> TexturePadding::clone() const
{
  performance::Scope timing("padding_geometry_clone");
  return std::make_shared<TexturePadding>(impl_->source, impl_->material_uid);
}
std::uint64_t TexturePadding::invocation_count()
{
  return padding_invocations.load();
}
std::vector<unsigned char> TexturePadding::uv_coverage(int resolution, int tile, int height)
{
  if (!height)
    height = resolution;
  if (resolution < 1 || height < 1)
    throw std::invalid_argument("Invalid export UV resolution");
  const auto count = resource_product(resolution, height);
  std::vector<unsigned char> result(count, 0);
  if (!impl_->mesh)
    return result;
  if (!impl_->mask_sizes.contains(tile) ||
      impl_->mask_sizes[tile] != std::pair{resolution, height} || !impl_->masks.contains(tile))
    impl_->rasterize(tile, resolution, height);
  const auto &mask = impl_->masks.at(tile);
  if (mask.size() != count)
    throw std::runtime_error("Export UV mask size mismatch");
  for (std::size_t i = 0; i < count; ++i)
    result[i] = mask[i] != 0;
  return result;
}
std::shared_ptr<const GpuMarginPlan> TexturePadding::gpu_margin_plan(int width,
                                                                     int height,
                                                                     int tile,
                                                                     bool adjacent)
{
  const auto key = std::tuple{width, height, tile, adjacent};
  if (auto found = impl_->gpu_margins.find(key); found != impl_->gpu_margins.end())
    return found->second;
  performance::Scope timing("gpu_margin_geometry_plan");
  const auto coverage = uv_coverage(width, tile, height);
  GpuMarginPlan result;
  if (adjacent && impl_->mesh) {
    const float offset[2] = {float((tile - 1001) % 10), float((tile - 1001) / 10)};
    const auto native = RE_texture_margin_plan(
        *impl_->mesh,
        "UVMap",
        Span<char>(reinterpret_cast<const char *>(coverage.data()), coverage.size()),
        width,
        height,
        2,
        offset);
    std::vector<GpuMarginPlan::Lookup> lookups;
    lookups.reserve(native.lookups.size());
    for (const auto &lookup : native.lookups)
      lookups.push_back({lookup.destination, lookup.x, lookup.y});
    result = GpuMarginPlan::compile(width,
                                    height,
                                    coverage,
                                    lookups,
                                    std::span<const char>(native.mask.data(), native.mask.size()));
  }
  else
    result = GpuMarginPlan::compile(width, height, coverage);
  auto plan = std::make_shared<const GpuMarginPlan>(std::move(result));
  impl_->gpu_margins[key] = plan;
  return plan;
}
void TexturePadding::apply(ImBuf &buffer, int tile, bool adjacent)
{
  if (!impl_->mesh)
    return;
  if (buffer.y < 1 || buffer.x < 1 || !buffer.float_data())
    throw std::runtime_error("Invalid native padding buffer");
  if (!impl_->mask_sizes.contains(tile) ||
      impl_->mask_sizes[tile] != std::pair{buffer.x, buffer.y})
    impl_->rasterize(tile, buffer.x, buffer.y);
  const std::size_t count = resource_product(buffer.x, buffer.y);
  auto found = impl_->masks.find(tile);
  if (found == impl_->masks.end()) {
    impl_->rasterize(tile, buffer.x, buffer.y);
    found = impl_->masks.find(tile);
  }
  std::unique_ptr<char[], decltype([](char *p) { MEM_delete(p); })> mask(
      MEM_new_array_uninitialized<char>(count, __func__));
  std::copy(found->second.begin(), found->second.end(), mask.get());
  const float offset[2] = {float((tile - 1001) % 10), float((tile - 1001) / 10)};
  ++padding_invocations;
  RE_bake_margin(&buffer,
                 mask.get(),
                 2,
                 adjacent ? R_BAKE_ADJACENT_FACES : R_BAKE_EXTEND,
                 impl_->mesh,
                 "UVMap",
                 offset);
}
std::vector<SkewSurfaceVector> TexturePadding::skew_vectors(int density,
                                                            const std::set<int> &tiles)
{
  if (density < 1)
    throw std::invalid_argument("Invalid skew vector density");
  std::vector<SkewSurfaceVector> result;
  if (!impl_->mesh)
    return result;
  const auto triangles = impl_->mesh->corner_tris();
  const auto vertices = impl_->mesh->corner_verts();
  const auto positions = impl_->mesh->vert_positions(), normals = impl_->mesh->vert_normals(),
             faces = impl_->mesh->face_normals();
  const auto face_ids = impl_->mesh->corner_tri_faces();
  for (int tile : tiles) {
    const auto pixels = impl_->rasterize(tile, density);
    for (int y = 0; y < density; ++y)
      for (int x = 0; x < density; ++x) {
        const auto &pixel = pixels[std::size_t(y) * density + x];
        if (pixel.primitive_id < 0)
          continue;
        const auto corners = triangles[pixel.primitive_id];
        SkewSurfaceVector sample;
        sample.tile = tile;
        sample.u = (x + .5) / density;
        sample.v = (y + .5) / density;
        sample.object = std::uint32_t(
            impl_->source.uv_triangles[face_ids[pixel.primitive_id]].vertex_ids[0] >> 32);
        float data[3][3];
        for (int k = 0; k < 3; ++k)
          std::copy_n(&positions[vertices[corners[k]]].x, 3, data[k]);
        interp_barycentric_tri_v3(data, pixel.uv[0], pixel.uv[1], sample.position.data());
        for (int k = 0; k < 3; ++k)
          std::copy_n(&normals[vertices[corners[k]]].x, 3, data[k]);
        interp_barycentric_tri_v3(data, pixel.uv[0], pixel.uv[1], sample.average.data());
        normalize_v3(sample.average.data());
        const auto normal = faces[face_ids[pixel.primitive_id]];
        sample.geometric = {normal.x, normal.y, normal.z};
        result.push_back(sample);
      }
  }
  return result;
}

}  // namespace bts
