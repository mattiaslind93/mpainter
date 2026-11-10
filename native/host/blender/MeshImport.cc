// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenPbrImportPolicy.hh"
#ifdef BTS_WITH_MATERIALX
#  include "OpenPbrUsdImport.hh"
#  include <QJsonDocument>
#endif
#include <QFileInfo>
#include "SceneBridge.hh"
#include "AutoUnwrap.hh"
#include <map>
#include <set>
#include "MeshImport.hh"
#include "SceneSnapshot.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>
#include <thread>
#include <utility>

#include "ABC_alembic.h"
#include "BKE_anim_data.hh"
#include "BKE_blendfile_link_append.hh"
#include "BKE_main_invariants.hh"
#include "DNA_collection_types.h"
#include "DNA_space_types.h"
#include "BLO_readfile.hh"
#include "TextureMemory.hh"
#include "BKE_constraint.h"
#include "BKE_layer.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "BKE_lib_id.hh"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"
#include "BKE_attribute.hh"
#include "BKE_context.hh"
#include "BKE_camera.h"
#include "BKE_collection.hh"
#include "BKE_idprop.hh"
#include "DEG_depsgraph_build.hh"
#include "DNA_camera_types.h"
#include "BLI_math_vector.h"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "DNA_node_types.h"
#include "NOD_defaults.hh"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"
#include "BKE_sound.hh"
#include "BLI_listbase.h"
#include "BLI_math_matrix.hh"
#include "BLI_path_utils.hh"
#include "BLI_set.hh"
#include "BLI_string.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"
#include "IO_wavefront_obj.hh"
#ifdef BTS_WITH_FBX
#  include "IO_fbx.hh"
#endif
#include "usd.hh"
#include "BKE_modifier.hh"
#include "DNA_modifier_types.h"
#include "opencolorio/OCIO_matrix.hh"

namespace bts {
namespace {
using namespace blender;
namespace fs = std::filesystem;

void white_fallback(Material &material)
{
  auto &tree = *material.nodetree;
  for (bNode &node : tree.nodes)
    if (STREQ(node.idname, "ShaderNodeBsdfPrincipled")) {
      auto *socket = bke::node_find_enabled_input_socket(node, "Base Color");
      if (socket && socket->default_value) {
        auto *value = static_cast<bNodeSocketValueRGBA *>(socket->default_value);
        std::fill_n(value->value, 4, 1.f);
        BKE_ntree_update_tag_socket_property(&tree, socket);
      }
    }
}
void check_cancelled(const std::stop_token cancellation)
{
  if (cancellation.stop_requested()) {
    throw MeshImportCancelled();
  }
}

// Blender stores per-mesh topology indices in signed 32-bit integers.
void check_index_count(std::uint64_t count, const char *label)
{
  if (count > std::uint64_t(std::numeric_limits<int>::max()))
    throw std::overflow_error(std::string(label) +
                              " cannot be represented by Blender's signed mesh indices");
}
void validate_mesh_storage(std::uint64_t vertices,
                           std::uint64_t corners,
                           std::uint64_t triangles,
                           std::uint64_t objects,
                           std::uint64_t materials)
{
  // Detect overflow of representable mesh storage independently of current RAM.
  auto cpu = resource_sum(resource_product(vertices, 64), resource_product(corners, 64));
  cpu = resource_sum(cpu, resource_product(triangles, sizeof(UvFaceTriangle)));
  cpu = resource_sum(cpu, resource_product(objects, sizeof(Object)));
  cpu = resource_sum(cpu, resource_product(materials, sizeof(Material)));
}

std::string utf8_path(const fs::path &path)
{
  const std::u8string value = path.u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

fs::path native_path(const std::string_view path)
{
  return fs::path(std::u8string_view(reinterpret_cast<const char8_t *>(path.data()), path.size()));
}

struct SourceFile {
  fs::path path;
  std::uintmax_t bytes;
  fs::file_time_type modified;
};

SourceFile source_file(const fs::path &path)
{
  std::error_code error;
  if (!fs::is_regular_file(path, error) || error) {
    throw std::runtime_error("Mesh import requires a readable regular file: " + utf8_path(path));
  }
  const std::uintmax_t bytes = fs::file_size(path, error);
  if (error) {
    throw std::runtime_error("Cannot read mesh source size: " + utf8_path(path));
  }

  const fs::file_time_type modified = fs::last_write_time(path, error);
  if (error) {
    throw std::runtime_error("Cannot inspect mesh source: " + utf8_path(path));
  }
  return {path, bytes, modified};
}

std::string_view trim(std::string_view text)
{
  while (!text.empty() && static_cast<unsigned char>(text.front()) <= ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && static_cast<unsigned char>(text.back()) <= ' ') {
    text.remove_suffix(1);
  }
  return text;
}

std::string_view take_word(std::string_view &text)
{
  text = trim(text);
  const auto end = std::find_if(
      text.begin(), text.end(), [](const unsigned char ch) { return ch <= ' '; });
  const std::size_t count = std::size_t(end - text.begin());
  const std::string_view word = text.substr(0, count);
  text.remove_prefix(count);
  return word;
}

// Counts records without interpreting indices, floats, material syntax or topology.
// Blender accepts backslash-newline continuations, including trailing whitespace.
template<typename Function>
void scan_lines(const SourceFile &source,
                const std::stop_token cancellation,
                const bool continuation_lines,
                const Function &visit)
{
  std::ifstream input(source.path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Cannot open OBJ source: " + utf8_path(source.path));
  }

  std::string buffer, logical;
  std::uint64_t bytes = 0;
  while (std::getline(input, buffer)) {
    check_cancelled(cancellation);
    bytes = resource_sum(bytes, buffer.size() + (input.eof() ? 0 : 1));
    if (bytes > source.bytes)
      throw std::runtime_error("OBJ source changed during import: " + utf8_path(source.path));
    std::string_view physical(buffer);
    if (physical.find('\0') != std::string_view::npos)
      throw std::runtime_error("OBJ source contains an embedded NUL byte");
    const std::string_view stripped = trim(physical);
    const bool continued = continuation_lines && !input.eof() && !stripped.empty() &&
                           stripped.back() == '\\';
    if (continued) {
      physical = physical.substr(0, physical.find_last_of('\\'));
    }

    logical.append(physical);
    if (continued) {
      logical += ' ';
      continue;
    }
    visit(std::string_view(logical));
    logical.clear();
  }
  if (input.bad() || (!input.eof() && input.fail()))
    throw std::runtime_error("Cannot read OBJ source: " + utf8_path(source.path));
  if (!logical.empty()) {
    visit(std::string_view(logical));
  }
}

struct SourceBudget {
  std::size_t polygons = 0;
  std::size_t corners = 0;
  std::vector<SourceFile> files;
  std::vector<std::string> warnings;
};

SourceBudget inspect_source(const std::string &path, const std::stop_token cancellation)
{
  const fs::path source_path = native_path(path);
  if (!source_path.is_absolute() || path.find('\0') != std::string::npos ||
      path.size() >= FILE_MAX)
  {
    throw std::invalid_argument("OBJ source must be an absolute UTF-8 path shorter than FILE_MAX");
  }
  SourceBudget budget;

  budget.files.push_back(source_file(source_path));
  std::size_t vertices = 0, uvs = 0, normals = 0, objects = 0, triangles = 0, line_corners = 0;
  std::set<std::string> materials, libraries;
  scan_lines(budget.files.front(), cancellation, true, [&](std::string_view line) {
    const std::string_view keyword = take_word(line);
    if (keyword == "v") {
      check_index_count(++vertices, "Source vertices");
    }
    else if (keyword == "vt") {
      check_index_count(++uvs, "Source UV coordinates");
    }
    else if (keyword == "vn") {
      check_index_count(++normals, "Source normals");
    }
    else if (keyword == "o") {
      ++objects;
    }
    else if (keyword == "f" || keyword == "l") {
      std::size_t corners = 0;
      while (!trim(line).empty()) {
        const std::string_view word = take_word(line);
        if (word.front() == '#') {
          break;
        }
        ++corners;
      }
      if (keyword == "f") {
        check_index_count(corners, "Corners per polygon");
        check_index_count(++budget.polygons, "Source polygons");
        budget.corners = resource_sum(budget.corners, corners);
        check_index_count(budget.corners, "Source polygon corners");
        triangles = resource_sum(triangles, corners > 2 ? corners - 2 : 0);
        if ((budget.polygons & 4095) == 0)
          validate_mesh_storage(vertices, budget.corners, triangles, objects, materials.size());
      }
      else {
        line_corners = resource_sum(line_corners, corners);
        check_index_count(line_corners, "Source polyline corners");
      }
    }
    else if (keyword == "usemtl") {
      materials.emplace(trim(line));
    }
    else if (keyword == "mtllib") {
      line = trim(line);
      if (line.size() > 2 && line.front() == '"' && line.back() == '"') {
        line.remove_prefix(1);
        line.remove_suffix(1);
      }
      libraries.emplace(line);
    }
  });
  validate_mesh_storage(vertices, budget.corners, triangles, objects, materials.size());
  if (!budget.polygons) {
    throw std::runtime_error("OBJ source contains no polygon records");
  }

  char fallback[FILE_MAX];
  STRNCPY(fallback, path.c_str());
  BLI_path_extension_replace(fallback, sizeof(fallback), ".mtl");
  std::error_code error;
  if (fs::exists(native_path(fallback), error)) {
    libraries.emplace(BLI_path_basename(fallback));
  }
  char directory[FILE_MAXDIR];
  BLI_path_split_dir_part(path.c_str(), directory, sizeof(directory));
  std::size_t material_bytes = 0;
  std::set<std::string> material_definitions, visited_paths;
  for (const std::string &library : libraries) {
    check_cancelled(cancellation);
    if (std::strlen(directory) + library.size() + 1 >= FILE_MAX) {
      throw std::runtime_error("OBJ material-library path exceeds FILE_MAX");
    }
    char material_path[FILE_MAX];
    BLI_path_join(material_path, sizeof(material_path), directory, library.c_str());
    BLI_path_normalize(material_path);
    if (!visited_paths.emplace(material_path).second) {
      continue;
    }
    const fs::path native_material_path = native_path(material_path);
    error.clear();
    if (!fs::exists(native_material_path, error) && !error) {
      budget.warnings.push_back("Material library is missing: " + std::string(material_path));
      continue;
    }
    SourceFile file = source_file(native_material_path);
    material_bytes = resource_sum(material_bytes, file.bytes);

    scan_lines(file, cancellation, false, [&](std::string_view line) {
      if (take_word(line) == "newmtl") {
        material_definitions.emplace(trim(line));
      }
    });
    budget.files.push_back(std::move(file));
  }
  return budget;
}

struct NativeReports {
  ReportList list{};
  NativeReports()
  {
    BKE_reports_init(&list, RPT_STORE | RPT_PRINT_HANDLED_BY_OWNER);
  }
  ~NativeReports()
  {
    BKE_reports_free(&list);
  }
};

struct RestoreAudioCallback {
  Main &live;
  ~RestoreAudioCallback()
  {
    BKE_sound_refresh_callback_bmain(&live);
  }
};

bool nondegenerate(const float3 &a, const float3 &b, const float3 &c)
{
  const double x1 = double(b.x) - a.x, y1 = double(b.y) - a.y, z1 = double(b.z) - a.z;
  const double x2 = double(c.x) - a.x, y2 = double(c.y) - a.y, z2 = double(c.z) - a.z;
  const double x = y1 * z2 - z1 * y2, y = z1 * x2 - x1 * z2, z = x1 * y2 - y1 * x2;
  return x * x + y * y + z * z > 0.0;
}

void validate_geometry(Main &main,
                       ImportSummary &summary,
                       const std::stop_token cancellation,
                       bool mesh_objects_only = false)
{

  std::size_t corners = 0, meshes_without_polygons = 0;
  Set<Material *> used_materials;
  std::map<Material *, std::set<int>> material_tiles;
  std::set<Material *> invalid_udim;
  for (Object &object : main.objects) {
    check_cancelled(cancellation);
    if (object.type == OB_CAMERA || object.type == OB_EMPTY ||
        (mesh_objects_only && object.type != OB_MESH))
      continue;
    ++summary.objects;
    if (object.type != OB_MESH || !object.data || GS(object.data->name) != ID_ME) {
      throw std::runtime_error("OBJ project requires mesh objects; curves are not supported");
    }
    Mesh &mesh = *id_cast<Mesh *>(object.data);
    const Span<float3> positions = mesh.vert_positions();
    const Span<int> corner_verts = mesh.corner_verts();
    const Span<int> offsets = mesh.face_offsets();
    summary.vertices += positions.size();
    summary.polygons += mesh.faces_num;
    corners += corner_verts.size();

    validate_mesh_storage(
        summary.vertices, corners, summary.triangles, summary.objects, summary.materials.size());

    for (const float3 &position : positions) {
      if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        throw std::runtime_error("OBJ contains non-finite mesh coordinates");
      }
    }
    for (const int vertex : corner_verts) {
      if (vertex < 0 || vertex >= positions.size()) {
        throw std::runtime_error("OBJ contains an invalid polygon vertex index");
      }
    }
    for (const int2 &edge : mesh.edges()) {
      if (edge.x < 0 || edge.y < 0 || edge.x >= positions.size() || edge.y >= positions.size()) {
        throw std::runtime_error("OBJ contains an invalid mesh edge");
      }
    }
    float transform[4][4];
    BKE_object_to_mat4(&object, transform);
    for (const auto &column : transform) {
      for (const float value : column) {
        if (!std::isfinite(value)) {
          throw std::runtime_error("OBJ contains a non-finite object transform");
        }
      }
    }

    const bke::AttributeAccessor attributes = mesh.attributes();
    const VectorSet<StringRefNull> uv_names = mesh.uv_map_names();
    for (const StringRefNull name : uv_names) {
      const bke::AttributeReader<float2> uv = attributes.lookup<float2>(name,
                                                                        bke::AttrDomain::Corner);
      if (!uv || uv.varray.size() != corner_verts.size()) {
        throw std::runtime_error("OBJ contains an invalid UV map");
      }
      const VArraySpan<float2> values(uv.varray);
      for (const float2 &value : values) {
        if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
          throw std::runtime_error("OBJ contains non-finite UV coordinates");
        }
      }
    }

    if (!mesh.faces_num) {
      ++meshes_without_polygons;
      continue;
    }
    if (offsets.size() != mesh.faces_num + 1 || offsets.first() != 0 ||
        offsets.last() != corner_verts.size())
    {
      throw std::runtime_error("OBJ contains invalid polygon boundaries");
    }
    for (int face = 0; face < mesh.faces_num; ++face) {
      if (offsets[face + 1] - offsets[face] < 3 || offsets[face] < 0 ||
          offsets[face + 1] > corner_verts.size())
      {
        throw std::runtime_error("OBJ contains an invalid polygon");
      }
    }
    const auto face_materials = mesh.attributes().lookup_or_default<int>(
        "material_index", bke::AttrDomain::Face, 0);
    const auto active_uv = attributes.lookup<float2>(mesh.active_or_default_uv_map_name(),
                                                     bke::AttrDomain::Corner);
    for (int face = 0; face < mesh.faces_num; ++face) {
      const int slot = std::clamp(
          face_materials.varray[face], 0, std::max(0, int(object.totcol) - 1));
      if (auto *material = BKE_object_material_get(&object, slot + 1)) {
        used_materials.add(material);
        const auto &uv = active_uv;
        if (!uv) {
          invalid_udim.insert(material);
          continue;
        }
        float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
        for (int corner : mesh.faces()[face]) {
          const auto value = uv.varray[corner];
          u0 = std::min(u0, value.x);
          v0 = std::min(v0, value.y);
          u1 = std::max(u1, value.x);
          v1 = std::max(v1, value.y);
        }
        if (u0 < 0 || v0 < 0 || u1 > 10 || v1 > 100) {
          invalid_udim.insert(material);
          continue;
        }
        const int x0 = int(std::floor(u0)), y0 = int(std::floor(v0)),
                  x1 = std::max(x0, int(std::ceil(u1)) - 1),
                  y1 = std::max(y0, int(std::ceil(v1)) - 1);
        if (x0 > 9 || y0 > 99) {
          invalid_udim.insert(material);
          continue;
        }
        auto &tiles = material_tiles[material];
        for (int y = y0; y <= y1; ++y)
          for (int x = x0; x <= x1; ++x)
            tiles.insert(1001 + x + 10 * y);
      }
    }
    ++summary.polygon_meshes;
    summary.meshes_with_uvs += !uv_names.is_empty();
    const std::size_t triangles = std::size_t(corner_verts.size()) - 2 * mesh.faces_num;
    validate_mesh_storage(summary.vertices,
                          corners,
                          resource_sum(summary.triangles, triangles),
                          summary.objects,
                          used_materials.size());
    const Span<int3> tessellation = mesh.corner_tris();
    if (tessellation.size() != triangles) {
      throw std::runtime_error("OBJ polygon tessellation is incomplete");
    }
    summary.triangles += tessellation.size();
    for (const int3 &triangle : tessellation) {
      const float3 &a = positions[corner_verts[triangle.x]];
      const float3 &b = positions[corner_verts[triangle.y]];
      const float3 &c = positions[corner_verts[triangle.z]];
      summary.nondegenerate_triangles += nondegenerate(a, b, c);
    }
  }
  if (!summary.polygon_meshes || !summary.nondegenerate_triangles) {
    throw std::runtime_error("OBJ contains no finite nondegenerate polygon mesh");
  }
  for (Material &material : main.materials) {
    if (!used_materials.contains(&material))
      continue;
    summary.materials.push_back({material.id.session_uid,
                                 material.id.name + 2,
                                 material_tiles[&material],
                                 !invalid_udim.contains(&material)});
  }
  summary.uv_state = summary.meshes_with_uvs == summary.polygon_meshes ?
                         ImportedUvState::AllPolygonMeshes :
                     summary.meshes_with_uvs ? ImportedUvState::SomePolygonMeshes :
                                               ImportedUvState::None;
  if (summary.meshes_with_uvs < summary.polygon_meshes) {
    summary.warnings.push_back(std::to_string(summary.polygon_meshes - summary.meshes_with_uvs) +
                               " polygon mesh(es) have no UV map");
  }
  if (meshes_without_polygons) {
    summary.warnings.push_back(std::to_string(meshes_without_polygons) +
                               " mesh object(s) have no polygons and were retained");
  }
  if (summary.nondegenerate_triangles < summary.triangles) {
    summary.warnings.push_back(
        std::to_string(summary.triangles - summary.nondegenerate_triangles) +
        " degenerate triangle(s) were retained");
  }
}

}  // namespace

struct PendingMeshProject::Impl {
  const std::thread::id owner = std::this_thread::get_id();
  Main *live = nullptr;
  Main *main = nullptr;
  Scene *scene = nullptr;
  bContext *context = nullptr;
  ImportSummary summary;
  std::unique_ptr<ConvertedScene> conversion;
  std::unique_ptr<FrozenUsdScene> frozen_usd;
  State state = State::Ready;

  void freeze_surface(bool cameras,
                      std::stop_token cancellation,
                      std::optional<double> frame = std::nullopt,
                      bool surface_objects_only = false);

  void require_owner() const
  {
    if (std::this_thread::get_id() != owner) {
      throw std::logic_error("Mesh import accessed outside its native owner thread");
    }
  }

  ~Impl()
  {
    if (std::this_thread::get_id() != owner) {
      std::terminate();
    }
    if (context) {
      CTX_free(context);
    }
    if (main) {
      BKE_main_free(main);
    }
  }
};

void PendingMeshProject::Impl::freeze_surface(bool cameras,
                                              std::stop_token cancellation,
                                              std::optional<double> frame,
                                              bool surface_objects_only)
{

  // Keep the authored cage; viewport tessellation is owned by the application.
  for (Object &object : main->objects)
    if (auto *modifier = BKE_modifiers_findby_type(&object, eModifierType_Subsurf)) {
      auto *subdivision = reinterpret_cast<SubsurfModifierData *>(modifier);
      subdivision->levels = 0;
      subdivision->renderLevels = 0;
    }
  ImportSummary preflight;
  validate_geometry(*main, preflight, cancellation, surface_objects_only);
  auto *layer = static_cast<ViewLayer *>(scene->view_layers.first);
  BKE_view_layer_synced_ensure(*main, scene, layer);
  std::unique_ptr<Depsgraph, decltype(&DEG_graph_free)> graph(
      DEG_graph_new(main, scene, layer, DAG_EVAL_VIEWPORT), DEG_graph_free);
  if (frame)
    BKE_scene_frame_set(scene, float(*frame));
  DEG_graph_build_from_view_layer(graph.get());
  DEG_evaluate_on_framechange(graph.get(), BKE_scene_frame_get(scene));
  struct FrozenObject {
    Object *object;
    Mesh *mesh;
    float4x4 world;
  };
  std::vector<FrozenObject> frozen;
  for (Object &object : main->objects) {
    check_cancelled(cancellation);
    auto *evaluated = DEG_get_evaluated(graph.get(), &object);
    Mesh *mesh = object.type == OB_MESH ?
                     BKE_mesh_new_from_object_to_bmain(main, graph.get(), evaluated, true) :
                     nullptr;
    frozen.push_back({&object, mesh, evaluated->object_to_world()});
  }
  graph.reset();
  // Alembic face sets name material slots, but do not carry shader networks.
  // Native reader placeholders need editable Principled nodes as well as empty slots.
  for (Material &material : main->materials)
    if (!material.nodetree || !material.nodetree->nodes.first) {
      nodes::node_tree_shader_default(context, main, &material.id);
      white_fallback(material);
    }
  Material *fallback = nullptr;
  for (auto &entry : frozen) {
    Object &object = *entry.object;
    if (surface_objects_only && object.type != OB_MESH && object.type != OB_CAMERA &&
        object.type != OB_EMPTY)
    {
      BKE_id_delete(main, &object.id);
      continue;
    }
    BKE_object_free_modifiers(&object, 0);
    BKE_constraints_free(&object.constraints);
    BKE_animdata_free(&object.id, true);
    object.parent = nullptr;
    BKE_object_apply_mat4(&object, entry.world.ptr(), false, false);
    IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_scene_object"));
    if (object.type == OB_CAMERA) {
      if (!cameras) {
        BKE_id_delete(main, &object.id);
        continue;
      }
      IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_camera"));
      ++summary.cameras;
      summary.camera_names[object.id.session_uid] = object.id.name + 2;
    }
    if (!entry.mesh)
      continue;
    id_us_min(object.data);
    object.data = &entry.mesh->id;
    id_us_plus(object.data);
    for (int slot = 1; slot <= std::max(1, int(object.totcol)); ++slot)
      if (!BKE_object_material_get(&object, slot)) {
        if (!fallback) {
          fallback = BKE_material_add(main, "DefaultMaterial");
          nodes::node_tree_shader_default(context, main, &fallback->id);
          white_fallback(*fallback);
        }
        BKE_object_material_assign_single_obdata(main, &object, fallback, slot);
      }
  }
  // Orphan native cache handles must not become external project dependencies.
  while (main->cachefiles.first)
    BKE_id_delete(main, main->cachefiles.first);
}

MeshImportCancelled::MeshImportCancelled() : std::runtime_error("OBJ import cancelled") {}

PendingMeshProject::PendingMeshProject(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PendingMeshProject::~PendingMeshProject() = default;

std::unique_ptr<PendingMeshProject> PendingMeshProject::from_scene(Main &live,
                                                                   const std::string &path,
                                                                   bool cameras,
                                                                   std::stop_token cancellation,
                                                                   const UsdImportOptions &usd,
                                                                   const BlendMeshSelection &blend)
{
  if (QFileInfo(QString::fromStdString(path)).suffix().compare("fbx", Qt::CaseInsensitive) == 0)
    return from_fbx(live, path, cameras, cancellation);
  if (is_blend_library(path))
    return from_blend(live, path, cameras, blend, cancellation);
  if (QFileInfo(QString::fromStdString(path)).suffix().compare("abc", Qt::CaseInsensitive) == 0)
    return from_alembic(live, path, cameras, cancellation);
  if (is_usd_scene(path))
    return from_usd(live, path, cameras, usd, cancellation);
  if (QFileInfo(QString::fromStdString(path)).suffix().compare("obj", Qt::CaseInsensitive) == 0)
    return from_obj(live, path, cancellation);
  check_cancelled(cancellation);
  auto conversion = convert_mesh_scene(path, cameras);
  check_cancelled(cancellation);
  auto pending = from_obj(live, conversion->obj_path, cancellation);
  auto &p = *pending->impl_;
  // Embedded glTF images are staging files; pack their original encoded bytes
  // before the temporary directory can disappear at merge/destruction.
  if (!conversion->embedded_images.empty()) {
    NativeReports reports;
    for (Image &image : p.main->images) {
      const std::string filename = image.filepath;
      if (std::find(conversion->embedded_images.begin(),
                    conversion->embedded_images.end(),
                    filename) == conversion->embedded_images.end())
        continue;
      BKE_image_packfiles(&reports.list, &image, p.main->filepath);
      if (BLI_listbase_is_empty(&image.packedfiles))
        throw std::runtime_error("Cannot retain embedded scene texture: " + filename);
    }
  }
  p.summary.source_path = path;
  p.summary.source_units = inspect_source_units(path);
  for (const auto &source : conversion->cameras) {
    auto *camera = BKE_camera_add(p.main, source.name.c_str());
    camera->type = source.orthographic ? CAM_ORTHO : CAM_PERSP;
    camera->sensor_fit = CAMERA_SENSOR_FIT_VERT;
    camera->sensor_y = 24;
    camera->sensor_x = source.aspect > 0 ? 24 * source.aspect : 36;
    camera->lens = source.orthographic ?
                       50.f :
                       float(12 / std::tan(source.vertical_fov * 3.14159265358979323846 / 360));
    camera->ortho_scale = source.orthographic_extent;
    camera->clip_start = source.near_clip;
    camera->clip_end = source.far_clip;
    IDPropertyTemplate aspect{};
    aspect.d = source.aspect;
    IDP_AddToGroup(IDP_EnsureProperties(&camera->id),
                   IDP_New(IDP_DOUBLE, &aspect, "bts_imported_aspect"));
    auto *object = BKE_object_add_only_object(p.main, OB_CAMERA, source.name.c_str());
    object->data = &camera->id;
    float forward[3], up[3], right[3], matrix[4][4];
    copy_v3_v3(forward, source.forward.data());
    copy_v3_v3(up, source.up.data());
    if (normalize_v3(forward) == 0)
      throw std::runtime_error("Imported camera has zero view direction");
    cross_v3_v3v3(right, forward, up);
    if (normalize_v3(right) == 0)
      throw std::runtime_error("Imported camera up is parallel to view direction");
    cross_v3_v3v3(up, right, forward);
    unit_m4(matrix);
    copy_v3_v3(matrix[0], right);
    copy_v3_v3(matrix[1], up);
    negate_v3_v3(matrix[2], forward);
    copy_v3_v3(matrix[3], source.position.data());
    BKE_object_apply_mat4(object, matrix, false, false);
    IDP_AddToGroup(IDP_EnsureProperties(&object->id), IDP_NewInt(1, "bts_imported_camera"));
    BKE_collection_object_add(p.main, p.scene->master_collection, object);
    ++p.summary.cameras;
    p.summary.camera_names[object->id.session_uid] = source.name;
  }
  DEG_relations_tag_update(p.main);
  p.conversion = std::move(conversion);
  return pending;
}

std::unique_ptr<PendingMeshProject> PendingMeshProject::from_blend(
    Main &live,
    const std::string &path,
    bool cameras,
    const BlendMeshSelection &selection,
    std::stop_token cancellation)
{
  check_cancelled(cancellation);
  selection.validate(true);
  const auto catalog = inspect_blend_library(path, cancellation);
  for (const auto &[chosen, available] : {std::pair{&selection.objects, &catalog.objects},
                                          std::pair{&selection.collections, &catalog.collections}})
    for (const auto &name : *chosen)
      if (!std::binary_search(available->begin(), available->end(), name))
        throw std::invalid_argument("Blender library entry no longer exists: " + name);
  const auto source = source_file(native_path(path));
  auto pending = std::make_unique<Impl>();
  pending->live = &live;
  pending->main = BKE_main_new();
  IMB_colormanagement_working_space_convert(pending->main, &live);
  pending->scene = BKE_scene_add(pending->main, "Imported Blender mesh");
  pending->context = CTX_create();
  {
    RestoreAudioCallback restore{live};
    CTX_data_main_set(pending->context, pending->main);
  }
  CTX_data_scene_set(pending->context, pending->scene);
  auto *layer = static_cast<ViewLayer *>(pending->scene->view_layers.first);
  LibraryLink_Params parameters;
  BLO_library_link_params_init_with_context(
      &parameters, pending->main, BLO_LIBLINK_APPEND_RECURSIVE, 0, pending->scene, layer, nullptr);
  BKE_main_id_tag_all(pending->main, ID_TAG_PRE_EXISTING, true);
  std::unique_ptr<BlendfileLinkAppendContext, decltype(&BKE_blendfile_link_append_context_free)>
      append(BKE_blendfile_link_append_context_new(&parameters),
             BKE_blendfile_link_append_context_free);
  BKE_blendfile_link_append_context_library_add(append.get(), path.c_str(), nullptr);
  std::vector<BlendfileLinkAppendContextItem *> items;
  for (const auto &[names, type] : {std::pair{&selection.objects, short(ID_OB)},
                                    std::pair{&selection.collections, short(ID_GR)}})
    for (const auto &name : *names) {
      auto *item = BKE_blendfile_link_append_context_item_add(
          append.get(), name.c_str(), type, nullptr);
      BKE_blendfile_link_append_context_item_library_index_enable(append.get(), item, 0);
      items.push_back(item);
    }
  NativeReports reports;
  BKE_blendfile_link(append.get(), &reports.list);
  check_cancelled(cancellation);
  BKE_blendfile_append(append.get(), &reports.list);
  BKE_blendfile_link_append_instantiate_loose(append.get(), &reports.list);
  // Append IDs are local and native color conversion already used FileGlobal's
  // source primaries. Do not manually convert Rec.709 a second time.
  std::set<Object *> selected;
  for (auto *item : items) {
    auto *id = BKE_blendfile_link_append_context_item_newid_get(append.get(), item);
    if (!id)
      throw std::runtime_error("Cannot append Blender library entry: " + item->name);
    if (GS(id->name) == ID_OB)
      selected.insert(reinterpret_cast<Object *>(id));
    else {
      auto *collection = reinterpret_cast<Collection *>(id);
      FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, object) {
        selected.insert(object);
      }
      FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
    }
  }
  BKE_main_ensure_invariants(*pending->main);
  append.reset();
  check_cancelled(cancellation);
  if (BKE_reports_contain(&reports.list, RPT_ERROR))
    throw std::runtime_error("Blender reported an error while appending the selection");
  for (auto *object : selected)
    if (object->instance_collection)
      throw std::invalid_argument(
          "Choose the source collection rather than an instanced collection object");

  // Import the original mesh cage. Never execute source geometry-node graphs,
  // modifiers or retain external cache readers in a new painting project.
  for (Object &object : pending->main->objects) {
    BKE_object_free_modifiers(&object, 0);
    object.visibility_flag = eObject_VisibilityFlag(0);
  }
  while (pending->main->shapekeys.first)
    BKE_id_delete(pending->main, pending->main->shapekeys.first);
  for (Collection &collection : pending->main->collections)
    collection.flag &= ~(COLLECTION_HIDE_VIEWPORT | COLLECTION_HIDE_RENDER);
  ImportSummary preflight;
  validate_geometry(*pending->main, preflight, cancellation, true);
  BKE_view_layer_synced_ensure(*pending->main, pending->scene, layer);
  std::unique_ptr<Depsgraph, decltype(&DEG_graph_free)> graph(
      DEG_graph_new(pending->main, pending->scene, layer, DAG_EVAL_VIEWPORT), DEG_graph_free);
  DEG_graph_build_from_view_layer(graph.get());
  DEG_evaluate_on_framechange(graph.get(), 1.0f);
  std::map<Object *, float4x4> transforms;
  for (auto *object : selected)
    if (object->type == OB_MESH || (cameras && object->type == OB_CAMERA)) {
      check_cancelled(cancellation);
      transforms.emplace(object, DEG_get_evaluated(graph.get(), object)->object_to_world());
    }
  graph.reset();
  ID *animated;
  FOREACH_MAIN_ID_BEGIN (pending->main, animated) {
    BKE_animdata_free(animated, true);
  }
  FOREACH_MAIN_ID_END;
  std::vector<Object *> discarded;
  for (Object &object : pending->main->objects) {
    const auto found = transforms.find(&object);
    if (found == transforms.end()) {
      discarded.push_back(&object);
      continue;
    }
    BKE_constraints_free(&object.constraints);
    BKE_animdata_free(&object.id, true);
    object.parent = nullptr;
    BKE_object_apply_mat4(&object, found->second.ptr(), false, false);
    IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_scene_object"));
    if (object.type == OB_CAMERA) {
      IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_camera"));
      ++pending->summary.cameras;
      pending->summary.camera_names[object.id.session_uid] = object.id.name + 2;
    }
  }
  for (auto *object : discarded)
    BKE_id_delete(pending->main, &object->id);
  ID *id;
  FOREACH_MAIN_ID_BEGIN (pending->main, id) {
    if (ID_IS_LINKED(id))
      throw std::runtime_error("Selected Blender library has unresolved linked dependencies");
  }
  FOREACH_MAIN_ID_END;
  while (pending->main->libraries.first)
    BKE_id_delete(pending->main, pending->main->libraries.first);
  while (pending->main->cachefiles.first)
    BKE_id_delete(pending->main, pending->main->cachefiles.first);
  for (Material &material : pending->main->materials)
    if (!material.nodetree || material.nodetree->nodes.is_empty()) {
      MaterialValues values;
      values.base_color = {material.r, material.g, material.b, material.a};
      values.metallic = material.metallic;
      values.roughness = material.roughness;
      nodes::node_tree_shader_default(pending->context, pending->main, &material.id);
      write_material_values(*pending->main, material, values);
    }
  Material *fallback = nullptr;
  for (Object &object : pending->main->objects)
    if (object.type == OB_MESH)
      for (int slot = 1; slot <= std::max(1, int(object.totcol)); ++slot)
        if (!BKE_object_material_get(&object, slot)) {
          if (!fallback) {
            fallback = BKE_material_add(pending->main, "DefaultMaterial");
            nodes::node_tree_shader_default(pending->context, pending->main, &fallback->id);
            white_fallback(*fallback);
          }
          BKE_object_material_assign_single_obdata(pending->main, &object, fallback, slot);
        }
  const auto after = source_file(source.path);
  if (after.bytes != source.bytes || after.modified != source.modified)
    throw std::runtime_error("Blender library changed during append");
  pending->summary.source_path = path;
  pending->summary.source_units = {1, 100};
  pending->summary.authored_color_source =
      "Native Blender append with FileGlobal source working-space conversion";
  pending->summary.warnings.emplace_back(
      "Imported static original mesh cages and frame-1 transforms; source modifiers, shape keys, animation and scene display-unit settings are not imported.");
  validate_geometry(*pending->main, pending->summary, cancellation);
  DEG_relations_tag_update(pending->main);
  return std::unique_ptr<PendingMeshProject>(new PendingMeshProject(std::move(pending)));
}

// Alembic is adopted as a static painting surface at the archive's first frame.
// Cache readers and hierarchy are evaluated in staging, then frozen; opening the
// saved project never needs the source archive and cannot mutate the live scene.
std::unique_ptr<PendingMeshProject> PendingMeshProject::from_alembic(Main &live,
                                                                     const std::string &path,
                                                                     bool cameras,
                                                                     std::stop_token cancellation,
                                                                     std::optional<double> frame)
{
  check_cancelled(cancellation);
  if (frame && (!std::isfinite(*frame) || std::abs(*frame) > 1e6))
    throw std::invalid_argument("Invalid Alembic source frame");
  if (!native_path(path).is_absolute())
    throw std::invalid_argument("Alembic requires an absolute source path");
  const auto source = source_file(native_path(path));
  auto pending = std::make_unique<Impl>();
  pending->live = &live;
  pending->main = BKE_main_new();
  IMB_colormanagement_working_space_convert(pending->main, &live);
  pending->scene = BKE_scene_add(pending->main, "Imported Alembic mesh");
  pending->context = CTX_create();
  {
    RestoreAudioCallback restore{live};
    CTX_data_main_set(pending->context, pending->main);
  }
  CTX_data_scene_set(pending->context, pending->scene);
  // The synchronous reader only needs the manager for interface locking, never a
  // native window or a background job. The private context borrows, not owns it.
  CTX_wm_manager_set(pending->context, static_cast<wmWindowManager *>(live.wm.first));
  AlembicImportParams params{};
  params.global_scale = 1;
  params.paths.append(path);
  params.validate_meshes = true;
  params.set_frame_range = true;
  if (!ABC_import(pending->context, &params, false))
    throw std::runtime_error("Native Blender Alembic import failed");
  CTX_wm_manager_set(pending->context, nullptr);
  check_cancelled(cancellation);
  const auto current = source_file(source.path);
  if (current.bytes != source.bytes || current.modified != source.modified)
    throw std::runtime_error("Alembic source changed during import");
  pending->freeze_surface(cameras, cancellation, frame);
  pending->summary.source_path = path;
  pending->summary.source_units = {};
  pending->summary.authored_color_source =
      "Native Blender Alembic geometry/UV reader; white Principled fallback for missing materials";
  pending->summary.warnings.emplace_back(
      "Alembic imported as a static surface at the archive's first frame; units are unspecified (use Unit Settings).");
  validate_geometry(*pending->main, pending->summary, cancellation);
  DEG_relations_tag_update(pending->main);
  return std::unique_ptr<PendingMeshProject>(new PendingMeshProject(std::move(pending)));
}

std::unique_ptr<PendingMeshProject> PendingMeshProject::from_fbx(Main &live,
                                                                 const std::string &path,
                                                                 bool cameras,
                                                                 std::stop_token cancellation)
{
#ifndef BTS_WITH_FBX
  throw std::runtime_error("This Blender build does not include the native FBX importer");
#else
  check_cancelled(cancellation);
  if (!native_path(path).is_absolute())
    throw std::invalid_argument("FBX requires an absolute source path");
  const auto source = source_file(native_path(path));
  auto pending = std::make_unique<Impl>();
  pending->live = &live;
  pending->main = BKE_main_new();
  IMB_colormanagement_working_space_convert(pending->main, &live);
  pending->scene = BKE_scene_add(pending->main, "Imported FBX mesh");
  pending->context = CTX_create();
  {
    RestoreAudioCallback restore{live};
    CTX_data_main_set(pending->context, pending->main);
  }
  CTX_data_scene_set(pending->context, pending->scene);
  // BKE_scene_add creates a view layer tagged for deferred collection sync.
  // FBX_import immediately dereferences its active editable collection, unlike
  // importers that synchronize the staging scene themselves.
  ViewLayer *view_layer = CTX_data_view_layer(pending->context);
  if (!view_layer)
    throw std::runtime_error("Native FBX staging scene has no view layer");
  BKE_view_layer_synced_ensure(*pending->main, pending->scene, view_layer);
  if (!BKE_layer_collection_get_active(view_layer))
    throw std::runtime_error(
        "Native FBX staging scene has no active collection after synchronization");
  NativeReports reports;
  FBXImportParams params;
  if (path.size() >= sizeof(params.filepath) || path.find('\0') != std::string::npos)
    throw std::invalid_argument("FBX source path exceeds Blender FILE_MAX or contains NUL");
  STRNCPY(params.filepath, path.c_str());
  params.global_scale = 1;
  params.validate_meshes = true;
  params.import_subdivision = false;
  params.use_anim = false;
  params.reports = &reports.list;
  FBX_import(pending->context, params);
  check_cancelled(cancellation);
  for (const Report &report : reports.list.list) {
    if (report.type >= RPT_ERROR)
      throw std::runtime_error(std::string("Native FBX import: ") + report.message);
    if (report.type == RPT_WARNING)
      pending->summary.warnings.emplace_back(report.message);
  }
  const auto current = source_file(source.path);
  if (current.bytes != source.bytes || current.modified != source.modified)
    throw std::runtime_error("FBX source changed during import");
  pending->freeze_surface(cameras, cancellation, std::nullopt, true);
  pending->summary.source_path = path;
  // Blender's FBX reader normalizes declared source units to native meters.
  pending->summary.source_units = {1, 100};
  pending->summary.authored_color_source =
      "Native Blender FBX reader: materials and image tags retained";
  pending->summary.warnings.emplace_back(
      "FBX imported as a static surface in meters; animation and armature modifiers are not retained.");
  validate_geometry(*pending->main, pending->summary, cancellation);
  DEG_relations_tag_update(pending->main);
  return std::unique_ptr<PendingMeshProject>(new PendingMeshProject(std::move(pending)));
#endif
}

std::unique_ptr<PendingMeshProject> PendingMeshProject::from_usd(Main &live,
                                                                 const std::string &path,
                                                                 bool cameras,
                                                                 const UsdImportOptions &options,
                                                                 std::stop_token cancellation)
{
  check_cancelled(cancellation);
  auto pending = std::make_unique<Impl>();
  pending->live = &live;
  pending->frozen_usd = freeze_usd_scene(path, options);
  check_cancelled(cancellation);
  pending->main = BKE_main_new();
  IMB_colormanagement_working_space_convert(pending->main, &live);
  pending->scene = BKE_scene_add(pending->main, "Imported USD mesh");
  pending->context = CTX_create();
  {
    RestoreAudioCallback restore{live};
    CTX_data_main_set(pending->context, pending->main);
  }
  CTX_data_scene_set(pending->context, pending->scene);
  NativeReports reports;
  io::usd::USDImportParams params{};
  params.scale = 1;
  params.light_intensity_scale = 1;
  params.apply_unit_conversion_scale = true;
  params.mesh_read_flag = MOD_MESHSEQ_READ_VERT | MOD_MESHSEQ_READ_POLY | MOD_MESHSEQ_READ_UV |
                          MOD_MESHSEQ_READ_COLOR | MOD_MESHSEQ_READ_ATTRIBUTES;
  params.import_defined_only = true;
  params.import_visible_only = true;
  params.import_cameras = cameras;
  params.import_materials = true;
  params.import_meshes = true;
  params.import_subdivision = true;
  params.import_proxy = true;
  params.import_render = true;
  params.import_usd_preview = true;
  params.validate_meshes = true;
  params.mtl_purpose = io::usd::MtlPurpose::All;
  params.mtl_name_collision_mode = io::usd::MtlNameCollisionMode::MakeUnique;
  params.import_textures_mode = io::usd::TexImportMode::Pack;
  params.property_import_mode = io::usd::PropertyImportMode::None;
  params.prim_path_mask = pending->frozen_usd->population_mask;
  params.sequence_len = 1;
  if (!io::usd::USD_import(
          pending->context, pending->frozen_usd->path.c_str(), &params, false, &reports.list))
    throw std::runtime_error("Native Blender USD import failed");
  check_cancelled(cancellation);
  for (const Report &report : reports.list.list)
    if (report.type >= RPT_ERROR)
      throw std::runtime_error(std::string("Native USD import: ") + report.message);
  Material *fallback = nullptr;
  for (Object &object : pending->main->objects) {
    IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_scene_object"));
    if (object.type == OB_CAMERA) {
      IDP_AddToGroup(IDP_EnsureProperties(&object.id), IDP_NewInt(1, "bts_imported_camera"));
      ++pending->summary.cameras;
      pending->summary.camera_names[object.id.session_uid] = object.id.name + 2;
      continue;
    }
    if (object.type != OB_MESH)
      continue;
    if (auto *modifier = BKE_modifiers_findby_type(&object, eModifierType_Subsurf)) {
      auto *subdivision = reinterpret_cast<SubsurfModifierData *>(modifier);
      subdivision->levels = options.subdivision;
      subdivision->renderLevels = options.subdivision;
    }
    for (int slot = 1; slot <= std::max(1, int(object.totcol)); ++slot)
      if (!BKE_object_material_get(&object, slot)) {
        if (!fallback) {
          fallback = BKE_material_add(pending->main, "DefaultMaterial");
          nodes::node_tree_shader_default(pending->context, pending->main, &fallback->id);
          white_fallback(*fallback);
        }
        BKE_object_material_assign_single_obdata(pending->main, &object, fallback, slot);
      }
  }
  // Catmull-Clark produces one quad per original corner on its first step.
  // Check the aggregate before adoption, including n-gons and multiple objects.
  std::uint64_t evaluated_triangles = 0;
  for (Object &object : pending->main->objects) {
    if (object.type != OB_MESH)
      continue;
    const auto &mesh = *reinterpret_cast<Mesh *>(object.data);
    if (options.subdivision > 0 && BKE_modifiers_findby_type(&object, eModifierType_Subsurf)) {
      std::uint64_t faces = mesh.corners_num;
      for (int level = 1; level < options.subdivision; ++level)
        faces = resource_product(faces, 4);
      evaluated_triangles = resource_sum(evaluated_triangles, resource_product(2, faces));
    }
    else
      evaluated_triangles = resource_sum(evaluated_triangles,
                                         std::uint64_t(mesh.corners_num) - 2 * mesh.faces_num);
    validate_mesh_storage(0, resource_product(evaluated_triangles, 3), evaluated_triangles, 0, 0);
  }
#ifdef BTS_WITH_MATERIALX
  try {
    const auto profiles = read_openpbr_usd_profiles(
        QString::fromStdString(pending->frozen_usd->path));
    for (Material &material : pending->main->materials) {
      std::vector<const OpenPbrUsdProfile *> matches;
      const QString name = QString::fromUtf8(material.id.name + 2);
      for (const auto &profile : profiles)
        if (profile.material_path.section('/', -1) == name)
          matches.push_back(&profile);
      if (matches.empty())
        for (const auto &profile : profiles)
          if (profile.name == name)
            matches.push_back(&profile);
      if (matches.empty())
        continue;
      if (matches.size() != 1) {
        pending->summary.warnings.push_back("Ambiguous OpenPBR material name: " +
                                            name.toStdString() + "; native USD material retained");
        continue;
      }
      const auto &source = *matches.front();
      QString reason;
      bool activate = false;
      try {
        const auto constants = openpbr_import_constants(source.parameters);
        if (!source.images.empty())
          reason =
              "Image resources awaiting background import; native USD material retained until ready";
        else {
          auto values = constants.base;
          IMB_colormanagement_transform_v4(
              values.base_color.data(), "ACEScg", pending->main->colorspace.scene_linear_name);
          if (!native_principled_surface(material)) {
            if (material.nodetree)
              throw std::invalid_argument(
                  "Native USD fallback has an unsupported surface; source material retained");
            nodes::node_tree_shader_default(pending->context, pending->main, &material.id);
          }
          write_material_values(*pending->main, material, values);
          activate = true;
        }
      }
      catch (const std::exception &error) {
        reason = QString::fromUtf8(error.what());
      }
      QJsonObject metadata{{"version", 1},
                           {"parameters", source.parameters.to_json()},
                           {"source", QString::fromStdString(path)},
                           {"material", source.material_path},
                           {"activate", activate},
                           {"reason", reason},
                           {"bump_scale", source.bump_scale},
                           {"displacement_scale", source.displacement_scale},
                           {"displacement_midlevel", source.displacement_midlevel},
                           {"displacement_enabled", source.displacement_enabled}};
      QJsonObject images;
      for (const auto &[parameter, image] : source.images)
        images[QString::fromStdString(parameter)] = QJsonObject{
            {"file", image.binding.filename}, {"space", int(image.binding.space)}};
      metadata["images"] = images;
      const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
      auto *properties = IDP_EnsureProperties(&material.id);
      if (auto *old = IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr"))
        IDP_FreeFromGroup(properties, old);
      IDP_AddToGroup(properties, IDP_NewString(json.constData(), "bts_imported_openpbr"));
      if (!activate)
        pending->summary.warnings.push_back("OpenPBR " + name.toStdString() + ": " +
                                            reason.toStdString() + "; source parameters retained");
      std::printf("BTS_USD_OPENPBR_IMPORT material=%s activate=%d images=%zu reason=%s\n",
                  material.id.name + 2,
                  int(activate),
                  source.images.size(),
                  reason.toUtf8().constData());
    }
  }
  catch (const std::exception &error) {
    pending->summary.warnings.push_back(std::string("OpenPBR profile import deferred: ") +
                                        error.what() + "; native USD material retained");
  }
#endif
  pending->summary.source_path = path;
  {
    const double meter = pending->frozen_usd->info.meters_per_unit;
    pending->summary.source_units = {meter, 100 * meter};
  }
  pending->summary.authored_color_source =
      "Native Blender USD reader: USD color-space metadata converted to the live working space";
  validate_geometry(*pending->main, pending->summary, cancellation);
  DEG_relations_tag_update(pending->main);
  return std::unique_ptr<PendingMeshProject>(new PendingMeshProject(std::move(pending)));
}

std::unique_ptr<PendingMeshProject> PendingMeshProject::from_obj(
    Main &live, const std::string &absolute_source_path, const std::stop_token cancellation)
{
  check_cancelled(cancellation);
  const SourceBudget source = inspect_source(absolute_source_path, cancellation);
  auto pending = std::make_unique<Impl>();
  pending->live = &live;
  pending->main = BKE_main_new();
  IMB_colormanagement_working_space_convert(pending->main, &live);
  pending->scene = BKE_scene_add(pending->main, "Imported mesh");
  pending->context = CTX_create();
  {
    RestoreAudioCallback restore{live};
    CTX_data_main_set(pending->context, pending->main);
  }
  CTX_data_scene_set(pending->context, pending->scene);
  NativeReports reports;
  OBJImportParams parameters;
  STRNCPY(parameters.filepath, absolute_source_path.c_str());
  parameters.relative_paths = false;
  parameters.reports = &reports.list;
  check_cancelled(cancellation);
  OBJ_import(pending->context, &parameters);
  check_cancelled(cancellation);

  pending->summary.source_path = absolute_source_path;
  pending->summary.warnings = source.warnings;
  std::string errors;
  for (const Report &report : reports.list.list) {
    if (report.type >= RPT_ERROR) {
      errors += (errors.empty() ? "" : "\n") + std::string(report.message);
    }
    else if (report.type == RPT_WARNING) {
      pending->summary.warnings.emplace_back(report.message);
    }
  }
  if (!errors.empty()) {
    throw std::runtime_error("Native OBJ import failed: " + errors);
  }
  // OBJ permits unassigned faces. Give these a real native material for a texturable set.
  Material *fallback = nullptr;
  for (Object &object : pending->main->objects) {
    if (object.type != OB_MESH)
      continue;
    const int slots = std::max(1, int(object.totcol));
    for (int slot = 1; slot <= slots; ++slot) {
      if (slot <= object.totcol && BKE_object_material_get(&object, slot))
        continue;
      if (!fallback) {
        fallback = BKE_material_add(pending->main, "DefaultMaterial");
        nodes::node_tree_shader_default(pending->context, pending->main, &fallback->id);
        white_fallback(*fallback);
      }
      BKE_object_material_assign_single_obdata(pending->main, &object, fallback, slot);
    }
  }
  const float3x3 to_xyz = live.colorspace.scene_linear_to_xyz;
  for (int column_index = 0; column_index < 3; ++column_index) {
    const float3 column = to_xyz[column_index];
    if (!std::isfinite(column.x) || !std::isfinite(column.y) || !std::isfinite(column.z)) {
      throw std::runtime_error("OBJ import requires a finite native working-space matrix");
    }
  }
  if (std::abs(math::determinant(to_xyz)) < 1e-12f) {
    throw std::runtime_error("OBJ import requires an initialized native working space");
  }
  // MTL Kd/Ke are copied raw; OBJ vertex RGB is already decoded from sRGB by Blender.
  // Native node conversion skips Normal Map/Vector Displacement and leaves Image tags/pixels.
  IMB_colormanagement_working_space_convert(
      pending->main, math::invert(ocio::XYZ_TO_REC709), math::invert(to_xyz), false);
  if (fallback)
    white_fallback(*fallback);  // A newly authored default is neutral in the target working space.
  validate_geometry(*pending->main, pending->summary, cancellation);
  std::size_t imported_corners = 0;
  for (Mesh &mesh : pending->main->meshes) {
    imported_corners += mesh.corners_num;
  }
  if (pending->summary.polygons != source.polygons || imported_corners != source.corners) {
    pending->summary.warnings.push_back(
        "Native OBJ validation repaired or skipped polygon records (source " +
        std::to_string(source.polygons) + " polygons / " + std::to_string(source.corners) +
        " corners; imported " + std::to_string(pending->summary.polygons) + " polygons / " +
        std::to_string(imported_corners) + " corners)");
  }
  for (const SourceFile &file : source.files) {
    const SourceFile current = source_file(file.path);
    if (current.bytes != file.bytes || current.modified != file.modified) {
      throw std::runtime_error("OBJ source changed during import: " + utf8_path(file.path));
    }
  }
  check_cancelled(cancellation);
  return std::unique_ptr<PendingMeshProject>(new PendingMeshProject(std::move(pending)));
}

void PendingMeshProject::prepare_uvs(const AutoUnwrapOptions &options)
{
  auto &p = *impl_;
  p.require_owner();
  if (p.state != State::Ready)
    throw std::logic_error("Cannot unwrap consumed import");
  generate_missing_uvs(*p.main, *p.scene, p.context, options);
  ImportSummary updated;
  updated.source_path = p.summary.source_path;
  updated.source_units = p.summary.source_units;
  updated.cameras = p.summary.cameras;
  updated.camera_names = p.summary.camera_names;
  updated.warnings = p.summary.warnings;
  validate_geometry(*p.main, updated, {});
  if (updated.uv_state != ImportedUvState::AllPolygonMeshes)
    throw std::runtime_error("Mesh has no UV data; enable Auto-unwrap or supply UVs");
  p.summary = std::move(updated);
}

PendingMeshProject::State PendingMeshProject::state() const
{
  impl_->require_owner();
  return impl_->state;
}

const ImportSummary &PendingMeshProject::summary() const
{
  impl_->require_owner();
  return impl_->summary;
}

SceneSnapshot PendingMeshProject::snapshot() const
{
  impl_->require_owner();
  if (impl_->state != State::Ready)
    throw std::logic_error("Cannot inspect a consumed mesh import");
  return read_scene_snapshot(impl_->context, nullptr, true);
}

Scene &PendingMeshProject::merge_into(Main &live)
{
  Impl &pending = *impl_;
  pending.require_owner();
  if (pending.state != State::Ready) {
    throw std::logic_error("Mesh project has already been consumed");
  }
  if (&live != pending.live) {
    throw std::logic_error("Mesh project belongs to a different live Main");
  }
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      if (live.colorspace.scene_linear_to_xyz[column][row] !=
          pending.main->colorspace.scene_linear_to_xyz[column][row])
      {
        throw std::logic_error("Live working space changed after OBJ staging");
      }
    }
  }
  Set<ID *> force_merge;
  ID *id;
  FOREACH_MAIN_ID_BEGIN (pending.main, id) {
    if (ID_IS_LINKED(id)) {
      throw std::logic_error("Staged OBJ unexpectedly contains linked IDs");
    }
    force_merge.add(id);
  }
  FOREACH_MAIN_ID_END;
  NativeReports reports;
  MainMergeReport merge_report;
  merge_report.reports = &reports.list;
  for (Scene &scene : pending.main->scenes) {
    BKE_scene_free_depsgraph_hash(&scene);
  }
  CTX_free(pending.context);
  pending.context = nullptr;
  Scene *scene = pending.scene;
  BKE_main_merge(&live, &force_merge, &pending.main, merge_report);
  pending.scene = nullptr;
  pending.state = State::Consumed;
  return *scene;
}

}  // namespace bts
