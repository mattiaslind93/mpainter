// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeFragmentGeometry.hh"
#include "NativeSurfaceBasis.hh"
#include "NativeMaterialSourcePolicy.hh"
#include <NativeMaterialFragmentGpuCompiler.hh>  // final phase2 ABI, supplied by coherent integration
#include <ResourceAvailability.hh>
#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include "BKE_attribute.hh"
#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_customdata.hh"
#include "BKE_mesh.h"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_tangent.hh"
#include "BKE_object.hh"
#include "BLI_array.hh"
#include "BLI_listbase_iterator.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_offset_indices.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "draw_resource.hh"
namespace bts {
void validate_native_fragment_object_context(const NativeFragmentGeometryObject &object,
                                             const blender::ObjectInfos &infos)
{
  if (!object.object_uid)
    throw std::invalid_argument("Fragment object owner UID missing");
  for (const auto *matrix : {&object.object_to_world, &object.world_to_object})
    for (float x : *matrix)
      if (!std::isfinite(x))
        throw std::invalid_argument("Fragment object owner matrix is nonfinite");
  for (int i = 0; i < 4; ++i)
    if (!std::isfinite(infos.ob_color[i]))
      throw std::invalid_argument("Fragment object owner color is nonfinite");
  if (!std::isfinite(infos.random))
    throw std::invalid_argument("Fragment object owner random is nonfinite");
}
namespace {
using namespace blender;
void check_cancel(const std::atomic_bool &cancel)
{
  if (cancel.load(std::memory_order_relaxed))
    throw std::runtime_error("Fragment geometry preparation cancelled");
}
void owner_only()
{
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() != app->thread())
    throw std::logic_error("Fragment geometry capture requires the native owner");
}
void worker_only()
{
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread())
    throw std::logic_error("Fragment geometry preparation requires a worker");
}
std::uint32_t index_value(std::uint64_t value)
{
  if (value > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("Fragment geometry exceeds uint32 index API");
  return std::uint32_t(value);
}
template<class T> std::vector<T> copied(Span<T> data)
{
  return {data.begin(), data.end()};
}
struct UvCopy {
  std::string name;
  std::vector<float2> values;
  std::vector<NativeFragmentUvRequest> requests;
};
struct ObjectCopy {
  NativeFragmentGeometryObject object;
  ObjectInfos infos{};
  std::vector<float3> positions, vertex_normals, face_normals, corner_normals, generated;
  std::vector<int> offsets, corners, triangle_faces, selected;
  std::vector<int3> triangles;
  Array<bool> sharp_faces;
  std::vector<UvCopy> uv;
  std::size_t target_uv = 0;
};
std::uint64_t mesh_bytes(const Mesh &mesh)
{
  auto bytes = resource_product(mesh.verts_num, sizeof(float3) * 2);
  bytes = resource_sum(bytes, resource_product(mesh.faces_num, sizeof(float3) + sizeof(bool)));
  bytes = resource_sum(bytes, resource_product(std::uint64_t(mesh.faces_num) + 1, sizeof(int)));
  bytes = resource_sum(bytes, resource_product(mesh.corners_num, sizeof(float3) + sizeof(int)));
  return resource_sum(bytes,
                      resource_product(mesh.corner_tris().size(), sizeof(int3) + sizeof(int) * 2));
}
[[noreturn]] void missing_orco(const std::string &reason)
{
  throw NativeFragmentContextError(
      NativeFragmentContextCode::GeneratedOrco,
      "Fragment Generated requires authoritative ORCO (BTS_Coord_Generated): " + reason);
}
void free_orco_clone(Mesh *mesh)
{
  if (mesh) {
    mesh->texcomesh = nullptr;
    BKE_id_free_ex(nullptr, mesh, LIB_ID_FREE_NO_MAIN | LIB_ID_FREE_NO_USER_REFCOUNT, true);
  }
}
bool same_original_geometry(const Mesh &source,
                            const Mesh &original,
                            const std::atomic_bool &cancel)
{
  if (source.verts_num != original.verts_num || source.edges_num != original.edges_num ||
      source.faces_num != original.faces_num || source.corners_num != original.corners_num)
    return false;
  const auto p = source.vert_positions(), q = original.vert_positions();
  for (int i = 0; i < p.size(); ++i) {
    if ((i & 1023) == 0)
      check_cancel(cancel);
    for (int axis = 0; axis < 3; ++axis)
      if (p[i][axis] != q[i][axis])
        return false;
  }
  const auto a = source.edges(), b = original.edges();
  for (int i = 0; i < a.size(); ++i) {
    if ((i & 1023) == 0)
      check_cancel(cancel);
    if (a[i] != b[i])
      return false;
  }
  const auto same = [&](Span<int> a, Span<int> b) {
    for (int i = 0; i < a.size(); ++i) {
      if ((i & 1023) == 0)
        check_cancel(cancel);
      if (a[i] != b[i])
        return false;
    }
    return true;
  };
  return same(source.face_offsets(), original.face_offsets()) &&
         same(source.corner_verts(), original.corner_verts()) &&
         same(source.corner_edges(), original.corner_edges());
}
void copy_generated(ObjectCopy &copy,
                    const Object &object,
                    const Mesh &mesh,
                    const NativeFragmentGeometryRequest &request,
                    std::uint64_t &bytes,
                    std::uint64_t budget,
                    const std::atomic_bool &cancel)
{
  if (!request.generated_stream)
    return;
  auto *original_object = DEG_get_original(&object);
  const auto *original = original_object && original_object->data &&
                                 GS(original_object->data->name) == ID_ME ?
                             reinterpret_cast<const Mesh *>(original_object->data) :
                             nullptr;
  if (original)
    copy.object.original_mesh_uid = original->id.session_uid;
  const auto count = mesh.verts_num;
  bytes = resource_sum(bytes, resource_product(count, sizeof(float3)));
  const auto *orco = static_cast<const float3 *>(CustomData_get_layer(&mesh.vert_data, CD_ORCO));
  std::unique_ptr<Mesh, decltype(&free_orco_clone)> clone(nullptr, free_orco_clone),
      texclone(nullptr, free_orco_clone);
  if (orco)
    copy.object.generated_provenance = NativeFragmentGeneratedProvenance::CapturedOrco;
  else {
    if (!request.allow_original_orco)
      missing_orco("selected source has no CD_ORCO and original capture is not authorized");
    if (!original || !same_original_geometry(mesh, *original, cancel))
      missing_orco(
          "selected source lacks CD_ORCO and does not match ordered original topology/positions");
    if (original->texcomesh && original->texcomesh->verts_num > original->verts_num)
      missing_orco("original texcomesh vertex count exceeds source topology");
    auto temporary = resource_sum(mesh_bytes(*original), resource_product(count, sizeof(float3)));
    if (original->texcomesh)
      temporary = resource_sum(temporary, mesh_bytes(*original->texcomesh));

    clone.reset(BKE_mesh_copy_for_eval(*original));
    if (!clone)
      throw std::runtime_error("Cannot clone original ORCO capture source");
    // Stock texspace ensure may update lazy texspace values. Isolate texcomesh
    // too, so no authored source ID/custom-data/texspace value is changed.
    if (original->texcomesh) {
      texclone.reset(BKE_mesh_copy_for_eval(*original->texcomesh));
      if (!texclone)
        throw std::runtime_error("Cannot clone ORCO texcomesh");
      clone->texcomesh = texclone.get();
    }
    Object private_owner{};
    private_owner.type = OB_MESH;
    private_owner.data = &clone->id;
    BKE_mesh_orco_ensure(&private_owner, clone.get());
    check_cancel(cancel);
    orco = static_cast<const float3 *>(CustomData_get_layer(&clone->vert_data, CD_ORCO));
    if (!orco)
      missing_orco("stock original capture supplied no CD_ORCO");
    copy.object.generated_provenance = NativeFragmentGeneratedProvenance::StockOriginalOrco;
  }
  copy.generated.reserve(count);
  for (int i = 0; i < count; ++i) {
    if ((i & 1023) == 0)
      check_cancel(cancel);
    const auto value = orco[i] * .5f + float3(.5f);
    copy.generated.push_back(value);
  }
}
std::array<float, 2> tile_origin(int tile)
{
  if (tile < 1001)
    throw std::invalid_argument("Fragment geometry requires an actual UDIM number");
  const int value = tile - 1001, x = value % 10, y = value / 10;
  if (double(float(y)) != double(y))
    throw std::invalid_argument(
        "UDIM raster origin is not exactly representable by the float vertex API");
  return {float(x), float(y)};
}
// Only a conservative triangle/UDIM bin; final clipping is exclusively GPU work.
// Winding is computed before any UDIM origin subtraction, in double precision.
std::pair<bool, bool> intersects(const ObjectCopy &o, const UvCopy &uv, int triangle, int tile)
{
  const auto c = o.triangles[triangle];
  const auto a = uv.values[c[0]], b = uv.values[c[1]], d = uv.values[c[2]];
  const double area = (double(b.x) - a.x) * (double(d.y) - a.y) -
                      (double(b.y) - a.y) * (double(d.x) - a.x);
  if (!std::isfinite(area) || area == 0)
    return {false, false};
  const auto origin = tile_origin(tile);
  const double lowx = std::min({double(a.x), double(b.x), double(d.x)}),
               highx = std::max({double(a.x), double(b.x), double(d.x)});
  const double lowy = std::min({double(a.y), double(b.y), double(d.y)}),
               highy = std::max({double(a.y), double(b.y), double(d.y)});
  return {highx >= origin[0] && lowx <= double(origin[0]) + 1 && highy >= origin[1] &&
              lowy <= double(origin[1]) + 1,
          area < 0};
}
bool finite(const float3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float3 unit(const float3 &v, const float3 &fallback = float3(0, 0, 1))
{
  const auto n = native_surface_unit({v.x, v.y, v.z}, {fallback.x, fallback.y, fallback.z});
  return {n[0], n[1], n[2]};
}
float3 triangle_normal(const ObjectCopy &o, int triangle)
{
  const auto c = o.triangles[triangle];
  const double3 a(o.positions[o.corners[c[0]]]), b(o.positions[o.corners[c[1]]]),
      d(o.positions[o.corners[c[2]]]);
  const auto cross = math::cross(b - a, d - a);
  const double length = std::hypot(cross.x, cross.y, cross.z);
  if (!(length > 0) || !std::isfinite(length))
    return float3(0);
  return float3(cross / length);
}
}  // namespace
struct NativeFragmentGeometrySource::Impl {
  NativeFragmentGeometryRequest request;
  std::vector<ObjectCopy> objects;
  std::uint64_t bytes = 0, budget = 0;
};
struct NativeFragmentGeometry::Impl {
  std::shared_ptr<const NativeFragmentGeometrySource> source;
  std::vector<NativeFragmentGeometryObject> objects;
  std::vector<NativeFragmentGeometryStream> streams;
  std::vector<std::vector<std::array<float, 3>>> generated;
  std::vector<std::vector<NativeFragmentVertex>> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<NativeFragmentGeometryRange> ranges;
  std::uint64_t bytes = 0;
};
NativeFragmentGeometrySource::NativeFragmentGeometrySource(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}
NativeFragmentGeometrySource::~NativeFragmentGeometrySource() = default;
std::uint64_t NativeFragmentGeometrySource::revision() const
{
  return impl_->request.revision;
}
std::uint32_t NativeFragmentGeometrySource::material_uid() const
{
  return impl_->request.material_uid;
}
std::size_t NativeFragmentGeometrySource::object_count() const
{
  return impl_->objects.size();
}
std::uint64_t NativeFragmentGeometrySource::storage_bytes() const
{
  return impl_->bytes;
}
NativeFragmentGeometry::NativeFragmentGeometry(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
NativeFragmentGeometry::~NativeFragmentGeometry() = default;
std::uint64_t NativeFragmentGeometry::revision() const
{
  return impl_->source->revision();
}
std::uint32_t NativeFragmentGeometry::material_uid() const
{
  return impl_->source->material_uid();
}
std::uint64_t NativeFragmentGeometry::storage_bytes() const
{
  return impl_->bytes;
}
std::span<const NativeFragmentGeometryObject> NativeFragmentGeometry::objects() const
{
  return impl_->objects;
}
const blender::ObjectInfos &NativeFragmentGeometry::object_infos(std::size_t object) const
{
  return impl_->source->impl_->objects.at(object).infos;
}
std::span<const NativeFragmentGeometryStream> NativeFragmentGeometry::streams() const
{
  return impl_->streams;
}
std::span<const NativeFragmentVertex> NativeFragmentGeometry::vertices(std::size_t stream) const
{
  return impl_->vertices.at(stream);
}
bool NativeFragmentGeometry::generated_requested() const
{
  return impl_->source->impl_->request.generated_stream;
}
std::span<const std::array<float, 3>> NativeFragmentGeometry::generated(std::size_t stream) const
{
  if (stream >= impl_->streams.size())
    throw std::out_of_range("Generated stream outside geometry");
  if (!generated_requested())
    return {};
  return impl_->generated.at(stream);
}
void validate_native_fragment_generated_context(const NativeFragmentGeometry &geometry)
{
  if (!geometry.generated_requested() || geometry.streams().empty())
    missing_orco("geometry has no requested Generated stream");
  for (const auto &object : geometry.objects())
    if (object.generated_provenance == NativeFragmentGeneratedProvenance::None)
      missing_orco("resident owner has no captured ORCO provenance");
  for (std::size_t i = 0; i < geometry.streams().size(); ++i) {
    const auto values = geometry.generated(i);
    if (values.empty() || values.size() != geometry.vertices(i).size())
      missing_orco("resident Generated count differs from its corner stream");
  }
}
std::span<const std::uint32_t> NativeFragmentGeometry::indices() const
{
  return impl_->indices;
}
std::span<const NativeFragmentGeometryRange> NativeFragmentGeometry::ranges() const
{
  return impl_->ranges;
}
std::shared_ptr<const NativeFragmentGeometrySource> capture_native_fragment_geometry(
    blender::bContext *context,
    blender::View3D *view,
    const SourceMeshMap &sources,
    NativeFragmentGeometryRequest request,
    const std::atomic_bool &cancel)
{
  owner_only();
  check_cancel(cancel);
  if (!context || !request.material_uid || request.uv_requests.empty() || request.tiles.empty())
    throw std::invalid_argument(
        "Fragment geometry context, material, UV requests and UDIMs are required");
  auto validate_uv = [](const NativeFragmentUvRequest &uv) {
    if (uv.kind != NativeFragmentUvKind::Named && uv.kind != NativeFragmentUvKind::DefaultRender &&
        uv.kind != NativeFragmentUvKind::DisplayActive)
      throw std::invalid_argument("Fragment geometry UV request kind is invalid");
    if ((uv.kind == NativeFragmentUvKind::Named) != !uv.name.empty())
      throw std::invalid_argument("Fragment geometry UV request name/semantics mismatch");
  };
  for (const auto &uv : request.uv_requests)
    validate_uv(uv);
  validate_uv(request.target_uv);
  std::sort(request.tiles.begin(), request.tiles.end());
  request.tiles.erase(std::unique(request.tiles.begin(), request.tiles.end()),
                      request.tiles.end());
  for (const int tile : request.tiles)
    tile_origin(tile);
  auto result = std::make_unique<NativeFragmentGeometrySource::Impl>();
  result->request = std::move(request);
  result->bytes = resource_sum(
      sizeof(*result),
      resource_sum(resource_product(result->request.uv_requests.capacity(),
                                    sizeof(NativeFragmentUvRequest)),
                   resource_product(result->request.tiles.capacity(), sizeof(int))));
  for (const auto &r : result->request.uv_requests)
    result->bytes = resource_sum(result->bytes, r.name.capacity());
  result->bytes = resource_sum(result->bytes, result->request.target_uv.name.capacity());
  auto *main = CTX_data_main(context);
  auto *scene = CTX_data_scene(context);
  auto *layer = CTX_data_view_layer(context);
  if (!main || !scene || !layer)
    throw std::invalid_argument("Fragment geometry native scene is unavailable");
  auto *graph = CTX_data_ensure_evaluated_depsgraph(context);
  if (!graph)
    throw std::invalid_argument("Fragment geometry native scene is unavailable");
  BKE_view_layer_synced_ensure(*main, scene, layer);
  for (const Base &base : *BKE_view_layer_object_bases_get(layer)) {
    check_cancel(cancel);
    if (!BKE_base_is_visible(view, &base))
      continue;
    auto *object = DEG_get_evaluated(graph, base.object);
    if (object->type != OB_MESH ||
        !(BKE_object_visibility(object, DAG_EVAL_VIEWPORT) & OB_VISIBLE_SELF))
      continue;
    const auto object_uid = DEG_get_original(object)->id.session_uid;
    const Mesh *mesh = BKE_object_get_evaluated_mesh(object);
    if (const auto found = sources.find(object_uid); found != sources.end())
      mesh = found->second;
    if (!mesh)
      continue;
    const int slots = BKE_object_material_count_eval(*object, mesh->id);
    if (slots < 1)
      continue;
    const auto attributes = mesh->attributes();
    const VArraySpan<int> materials = *attributes.lookup_or_default<int>(
        "material_index", bke::AttrDomain::Face, 0);
    const auto triangles = mesh->corner_tris();
    const auto faces = mesh->corner_tri_faces();
    bool selected = false;
    const auto matches = [&](int triangle) {
      const int slot = std::clamp(materials[faces[triangle]], 0, slots - 1);
      auto *m = BKE_object_material_get_eval(*object, mesh->id, short(slot + 1));
      return m && DEG_get_original(m)->id.session_uid == result->request.material_uid;
    };
    for (int i = 0; i < triangles.size(); ++i)
      if (matches(i)) {
        selected = true;
        break;
      }
    if (!selected)
      continue;
    const auto next = resource_sum(result->bytes,
                                   resource_sum(sizeof(ObjectCopy) * 2, mesh_bytes(*mesh)));
    ObjectCopy copy;
    copy.object.object_uid = object_uid;
    copy.object.source_mesh_uid = mesh->id.session_uid;
    copy.object.source_override = sources.contains(object_uid);
    bool invertible = false;
    (void)math::invert(object->object_to_world(), invertible);
    if (!invertible)
      throw std::invalid_argument("Fragment geometry object transform is singular");
    std::copy_n(&object->object_to_world()[0][0], 16, copy.object.object_to_world.begin());
    std::copy_n(&object->world_to_object()[0][0], 16, copy.object.world_to_object.begin());
    for (float value : copy.object.object_to_world)
      if (!std::isfinite(value))
        throw std::invalid_argument("Fragment geometry transform is nonfinite");
    for (float value : copy.object.world_to_object)
      if (!std::isfinite(value))
        throw std::invalid_argument("Fragment geometry inverse transform is nonfinite");
    copy.infos.sync(
        draw::ObjectRef(object), layer->basact == &base, bool(object->mode & OB_MODE_EDIT));
    validate_native_fragment_object_context(copy.object, copy.infos);
    copy.positions = copied(mesh->vert_positions());
    copy.vertex_normals = copied(mesh->vert_normals());
    copy.face_normals = copied(mesh->face_normals());
    copy.corner_normals = copied(mesh->corner_normals());
    copy.offsets = copied(mesh->face_offsets());
    copy.corners = copied(mesh->corner_verts());
    copy.triangles = copied(triangles);
    copy.triangle_faces = copied(faces);
    copy.selected.reserve(triangles.size());
    for (int i = 0; i < triangles.size(); ++i) {
      if ((i & 1023) == 0)
        check_cancel(cancel);
      if (matches(i))
        copy.selected.push_back(i);
    }
    const VArraySpan<bool> sharp = *attributes.lookup_or_default<bool>(
        "sharp_face", bke::AttrDomain::Face, false);
    copy.sharp_faces = Array<bool>(mesh->faces_num);
    std::copy(sharp.begin(), sharp.end(), copy.sharp_faces.begin());
    result->bytes = next;
    copy_generated(copy, *object, *mesh, result->request, result->bytes, result->budget, cancel);
    const auto load_uv = [&](const NativeFragmentUvRequest &wanted, bool source_request) {
      check_cancel(cancel);
      const std::string name = wanted.kind == NativeFragmentUvKind::Named ?
                                   wanted.name :
                                   std::string(wanted.kind == NativeFragmentUvKind::DefaultRender ?
                                                   mesh->default_uv_map_name() :
                                                   mesh->active_uv_map_name());
      if (name.empty())
        throw std::invalid_argument("Fragment geometry requested UV identity is unavailable");
      auto found = std::find_if(
          copy.uv.begin(), copy.uv.end(), [&](const UvCopy &u) { return u.name == name; });
      if (found != copy.uv.end()) {
        if (source_request && std::find(found->requests.begin(), found->requests.end(), wanted) ==
                                  found->requests.end())
          found->requests.push_back(wanted);
        return std::size_t(found - copy.uv.begin());
      }
      const VArraySpan<float2> uv = *attributes.lookup<float2>(name, bke::AttrDomain::Corner);
      if (uv.size() != mesh->corners_num)
        throw std::invalid_argument("Fragment geometry exact UV layer is missing: " + name);
      result->bytes = resource_sum(
          result->bytes,
          resource_sum(resource_product(uv.size(), sizeof(float2)),
                       sizeof(UvCopy) * 2 + name.size() +
                           sizeof(NativeFragmentUvRequest) * result->request.uv_requests.size()));
      UvCopy u;
      u.name = name;
      if (source_request)
        u.requests.push_back(wanted);
      u.values.reserve(uv.size());
      for (const auto value : uv) {
        if ((u.values.size() & 1023) == 0)
          check_cancel(cancel);
        u.values.push_back(value);
      }
      copy.uv.push_back(std::move(u));
      return copy.uv.size() - 1;
    };
    copy.target_uv = load_uv(result->request.target_uv, false);
    for (const auto &wanted : result->request.uv_requests)
      load_uv(wanted, true);
    // Drop only triangles with unusable positions/coordinates. Do this once for
    // all UV streams, before sanitizing Mikk's full-mesh input. No source Mesh is changed.
    const float4x4 world(copy.object.object_to_world.data());
    std::erase_if(copy.selected, [&](int triangle) {
      for (int k = 0; k < 3; ++k) {
        const int corner = copy.triangles[triangle][k];
        if (!finite(math::transform_point(world, copy.positions[copy.corners[corner]])))
          return true;
        if (!copy.generated.empty() && !finite(copy.generated[copy.corners[corner]]))
          return true;
        for (const auto &uv : copy.uv) {
          const auto p = uv.values[corner];
          if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return true;
        }
      }
      return false;
    });
    for (auto &p : copy.positions)
      if (!finite(p))
        p = float3(0);
    for (auto &uv : copy.uv)
      for (auto &p : uv.values)
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
          p = float2(0);
    for (auto *normals : {&copy.vertex_normals, &copy.face_normals, &copy.corner_normals})
      for (auto &n : *normals)
        n = unit(n, float3(0));
    for (std::size_t i = 0; i < copy.triangles.size(); ++i) {
      const auto ng = unit(triangle_normal(copy, int(i)));
      const auto face = copy.triangle_faces[i];
      copy.face_normals[face] = unit(copy.face_normals[face], ng);
      for (int k = 0; k < 3; ++k) {
        const int corner = copy.triangles[i][k];
        const int vertex = copy.corners[corner];
        copy.vertex_normals[vertex] = unit(copy.vertex_normals[vertex], ng);
        copy.corner_normals[corner] = unit(copy.corner_normals[corner],
                                           copy.vertex_normals[vertex]);
      }
    }
    result->objects.push_back(std::move(copy));
  }
  if (result->objects.empty())
    throw std::invalid_argument("Fragment geometry material has no visible source triangles");
  std::sort(result->objects.begin(), result->objects.end(), [](const auto &a, const auto &b) {
    return a.object.object_uid < b.object.object_uid;
  });
  check_cancel(cancel);
  return std::shared_ptr<const NativeFragmentGeometrySource>(
      new NativeFragmentGeometrySource(std::move(result)));
}
std::shared_ptr<const NativeFragmentGeometry> prepare_native_fragment_geometry(
    std::shared_ptr<const NativeFragmentGeometrySource> source, const std::atomic_bool &cancel)
{
  worker_only();
  check_cancel(cancel);
  if (!source)
    throw std::invalid_argument("Fragment geometry source owner is missing");
  const auto &s = *source->impl_;
  std::uint64_t bytes = s.bytes, scratch = 0, index_count = 0, range_count = 0;
  // Compact physical/raster degeneracies identically in every source stream.
  // Stream-local indices and Generated corners are emitted from this same list.
  const auto physical_drawable = [](const ObjectCopy &o, int triangle) {
    return math::length_squared(triangle_normal(o, triangle)) > 0;
  };
  for (const auto &o : s.objects) {
    check_cancel(cancel);
    scratch = std::max(scratch,
                       resource_sum(resource_product(o.corners.size(), 256),
                                    resource_product(o.triangles.size(), 64 + sizeof(int))));
    const auto count = resource_product(o.selected.size(), 3);
    index_value(count);
    for (const auto &uv : o.uv) {
      if (uv.requests.empty())
        continue;
      bytes = resource_sum(bytes,
                           resource_product(uv.requests.size(), sizeof(NativeFragmentUvRequest)));
      for (const auto &r : uv.requests)
        bytes = resource_sum(bytes, r.name.size());
      bytes = resource_sum(bytes,
                           resource_sum(resource_product(count, sizeof(NativeFragmentVertex)),
                                        sizeof(NativeFragmentGeometryStream) * 2 + uv.name.size() +
                                            o.uv[o.target_uv].name.size() +
                                            s.request.target_uv.name.size() +
                                            sizeof(std::vector<NativeFragmentVertex>) * 2));
      if (s.request.generated_stream)
        bytes = resource_sum(bytes,
                             resource_sum(resource_product(count, sizeof(std::array<float, 3>)),
                                          sizeof(std::vector<std::array<float, 3>>) * 2));
      for (int tile : s.request.tiles)
        for (bool invert : {false, true}) {
          std::uint64_t n = 0;
          for (std::size_t i = 0; i < o.selected.size(); ++i) {
            if ((i & 1023) == 0)
              check_cancel(cancel);
            const auto hit = intersects(o, o.uv[o.target_uv], o.selected[i], tile);
            if (hit.first && hit.second == invert && physical_drawable(o, o.selected[i]))
              n = resource_sum(n, 3);
          }
          if (n) {
            index_count = resource_sum(index_count, n);
            ++range_count;
            index_value(index_count);
          }
        }
    }
  }
  bytes = resource_sum(
      bytes,
      resource_sum(resource_product(index_count, sizeof(std::uint32_t)),
                   resource_product(range_count, sizeof(NativeFragmentGeometryRange))));
  bytes = resource_sum(
      bytes, resource_product(s.objects.size(), sizeof(NativeFragmentGeometryObject) * 2));
  // The captured source is already resident and is excluded by current MemAvailable.
  // Enforce its aggregate cap separately; charge only NEW worker bytes to fresh RAM.

  if (!index_count)
    throw std::invalid_argument(
        "Fragment geometry has no UV-addressable triangles in the requested UDIMs");
  auto result = std::make_unique<NativeFragmentGeometry::Impl>();
  result->source = std::move(source);
  result->bytes = bytes;
  result->indices.resize(std::size_t(index_count));
  result->ranges.reserve(std::size_t(range_count));
  std::size_t cursor = 0;
  for (std::size_t oi = 0; oi < s.objects.size(); ++oi) {
    const auto &o = s.objects[oi];
    std::vector<int> selected;
    selected.reserve(o.selected.size());
    for (const int triangle : o.selected) {
      check_cancel(cancel);
      if (physical_drawable(o, triangle) &&
          std::any_of(s.request.tiles.begin(), s.request.tiles.end(), [&](int tile) {
            return intersects(o, o.uv[o.target_uv], triangle, tile).first;
          }))
        selected.push_back(triangle);
    }
    result->objects.push_back(o.object);
    const float4x4 world(o.object.object_to_world.data()),
        inverse(o.object.world_to_object.data());
    const auto normal_to_world = math::transpose(float3x3(inverse));
    for (const auto &uv : o.uv) {
      if (uv.requests.empty() || selected.empty())
        continue;
      check_cancel(cancel);
      const auto stream = result->streams.size();
      result->streams.push_back(
          {oi, uv.name, s.request.target_uv, o.uv[o.target_uv].name, uv.requests});
      const Span<float2> map(uv.values.data(), uv.values.size());
      const Span<float2> maps[] = {map};
      auto tangents = bke::mesh::calc_uv_tangents(
          Span<float3>(o.positions.data(), o.positions.size()),
          OffsetIndices<int>(Span<int>(o.offsets.data(), o.offsets.size())),
          Span<int>(o.corners.data(), o.corners.size()),
          Span<int3>(o.triangles.data(), o.triangles.size()),
          Span<int>(o.triangle_faces.data(), o.triangle_faces.size()),
          o.sharp_faces,
          Span<float3>(o.vertex_normals.data(), o.vertex_normals.size()),
          Span<float3>(o.face_normals.data(), o.face_normals.size()),
          Span<float3>(o.corner_normals.data(), o.corner_normals.size()),
          Span<Span<float2>>(maps, 1));
      check_cancel(cancel);
      if (tangents.size() != 1 || tangents[0].size() != o.corners.size())
        throw std::runtime_error("Stock Mikk tangent output has an unexpected corner layout");
      auto &vertices = result->vertices.emplace_back();
      vertices.resize(selected.size() * 3);
      if (s.request.generated_stream) {
        if (o.generated.size() != o.positions.size() ||
            o.object.generated_provenance == NativeFragmentGeneratedProvenance::None)
          missing_orco("captured source lost Generated provenance/count");
        result->generated.emplace_back(vertices.size());
      }
      for (std::size_t i = 0; i < selected.size(); ++i) {
        if ((i & 1023) == 0)
          check_cancel(cancel);
        const auto c = o.triangles[selected[i]];
        const auto ng = unit(normal_to_world * triangle_normal(o, selected[i]));
        for (int k = 0; k < 3; ++k) {
          auto &v = vertices[i * 3 + k];
          const auto p = math::transform_point(world, o.positions[o.corners[c[k]]]);
          const auto n = unit(normal_to_world * o.corner_normals[c[k]], ng);
          const auto raw = tangents[0][c[k]];
          const auto local_n = unit(o.corner_normals[c[k]]);
          const auto t = native_surface_tangent({raw.x, raw.y, raw.z, raw.w},
                                                {local_n.x, local_n.y, local_n.z});
          const auto coord = uv.values[c[k]];
          if (s.request.generated_stream) {
            const auto value = o.generated[o.corners[c[k]]];
            result->generated.back()[i * 3 + k] = {value.x, value.y, value.z};
          }
          const auto raster = o.uv[o.target_uv].values[c[k]];
          v.uv = {coord.x, coord.y};
          v.raster_uv = {raster.x, raster.y};
          v.position = {p.x, p.y, p.z};
          v.normal = v.initial_normal = {n.x, n.y, n.z};
          v.geometric_normal = {ng.x, ng.y, ng.z};
          v.tangent = t;
        }
      }
      for (int tile : s.request.tiles)
        for (bool invert : {false, true}) {
          const auto first = cursor;
          for (std::size_t i = 0; i < selected.size(); ++i) {
            if ((i & 1023) == 0)
              check_cancel(cancel);
            const auto hit = intersects(o, o.uv[o.target_uv], selected[i], tile);
            if (hit.first && hit.second == invert)
              for (int k = 0; k < 3; ++k)
                result->indices[cursor++] = index_value(i * 3 + k);
          }
          if (cursor != first)
            result->ranges.push_back({stream,
                                      tile,
                                      tile_origin(tile),
                                      index_value(first),
                                      index_value(cursor - first),
                                      invert});
        }
    }
  }
  if (cursor != index_count || result->ranges.size() != range_count)
    throw std::logic_error("Fragment geometry index plan changed during immutable preparation");
  check_cancel(cancel);
  return std::shared_ptr<const NativeFragmentGeometry>(
      new NativeFragmentGeometry(std::move(result)));
}
}  // namespace bts
