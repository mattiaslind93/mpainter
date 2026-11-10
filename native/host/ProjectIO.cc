// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#include "SafeImagePack.hh"
#include "SanitizeReadback.hh"
#include "DisplayPostEffects.hh"
#include "SubsurfacePreviewIO.hh"
#include "FillTextureIO.hh"
#include "StrokeIO.hh"
#include "SkewIO.hh"
#include "ProjectIO.hh"
#include "PaintRecording.hh"
#include "GpuProjectReadback.hh"
#include "DisplacementPreview.hh"
#include "ResourceIO.hh"
#include "MeshMapIO.hh"
#include "SymmetryIO.hh"
#include "MaterialInspection.hh"
#include "EnvironmentResource.hh"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>
#include "BKE_blendfile.hh"
#include "BKE_blender_version.h"
#include "BKE_global.hh"
#include "BKE_idprop.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_workspace.hh"
#include "BKE_wm_runtime.hh"
#include "DNA_windowmanager_types.h"
#include "DNA_workspace_types.h"
#include "BKE_report.hh"
#include "BKE_studiolight.h"
#include "BKE_world.h"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLO_readfile.hh"
#include "BLO_writefile.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_ID.h"
#include "DNA_packedFile_types.h"
#include "DNA_node_types.h"
#include "DNA_world_types.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
namespace bts {
using namespace blender;
namespace {
IDProperty *property(IDProperty *group, const char *name, int type)
{
  auto *p = group ? IDP_GetPropertyFromGroup(group, name) : nullptr;
  if (!p || p->type != type)
    throw std::runtime_error(std::string("Missing project property: ") + name);
  return p;
}
IDProperty *group(ID &id, const char *name)
{
  auto *root = IDP_EnsureProperties(&id);
  auto *p = IDP_GetPropertyFromGroup(root, name);
  if (p && p->type != IDP_GROUP)
    throw std::runtime_error("Project property type conflict");
  if (!p) {
    IDPropertyTemplate value{};
    p = IDP_New(IDP_GROUP, &value, name);
    IDP_ReplaceInGroup(root, p);
  }
  return p;
}
void reference(IDProperty *group, const std::string &name, ID *id)
{
  IDPropertyTemplate value{};
  value.id = id;
  IDP_ReplaceInGroup(group, IDP_New(IDP_ID, &value, name));
}
std::string key(const QUuid &id, int channel = -1, int tile = 1001)
{
  auto result = id.toString(QUuid::WithoutBraces).toStdString();
  if (channel >= 0)
    result += "_" + std::to_string(channel);
  if (tile != 1001)
    result += "_" + std::to_string(tile);
  return result;
}
struct Buffer {
  Image *image;
  ImBuf *buffer;
  explicit Buffer(Image *image)
      : image(image), buffer(BKE_image_acquire_ibuf(image, nullptr, nullptr))
  {
  }
  ~Buffer()
  {
    if (buffer)
      BKE_image_release_ibuf(image, buffer, nullptr);
  }
};
struct Reports {
  ReportList list{};
  Reports()
  {
    BKE_reports_init(&list, RPT_STORE);
  }
  ~Reports()
  {
    BKE_reports_free(&list);
  }
};
}  // namespace
namespace {
void pack_environment_source(Main &main,
                             Scene &scene,
                             bool follows_view,
                             const std::string &source_path,
                             const std::string &source_name)
{
  auto *root = IDP_EnsureProperties(&scene.id);
  IDP_ReplaceInGroup(
      root, bke::idprop::create_bool("bts_lighting_follows_view", follows_view).release());
  QFile source(QString::fromStdString(source_path));
  if (!source.open(QIODevice::ReadOnly) || source.size() <= 0)
    throw std::runtime_error("Environment source is missing or empty");
  require_packed_file_size(source.size(), "Environment image");
  const auto bytes = source.readAll();
  if (bytes.size() != source.size())
    throw std::runtime_error("Environment source read was incomplete");
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  auto *existing = IDP_GetPropertyFromGroup(root, "bts_environment");
  ID *previous = existing && existing->type == IDP_ID ? IDP_ID_get(existing) : nullptr;
  Image *image = previous && GS(previous->name) == ID_IM ? reinterpret_cast<Image *>(previous) :
                                                           nullptr;
  auto *packed = image ? static_cast<ImagePackedFile *>(image->packedfiles.first) : nullptr;
  if (!packed || !packed->packedfile || packed->packedfile->size != bytes.size() ||
      QCryptographicHash::hash(QByteArray(static_cast<const char *>(packed->packedfile->data),
                                          packed->packedfile->size),
                               QCryptographicHash::Sha256)
              .toHex() != hash)
  {
    image = BKE_image_load(&main, source_path.c_str());
    if (!image)
      throw std::runtime_error("Cannot load the project environment for packing");
    BKE_image_packfiles(nullptr, image, main.filepath);
    if (image->packedfiles.is_empty())
      throw std::runtime_error("Cannot pack the project environment");
  }
  reference(root, "bts_environment", &image->id);
  IDP_ReplaceInGroup(root, IDP_NewString(source_name.c_str(), "bts_environment_name"));
  IDP_ReplaceInGroup(root, IDP_NewString(hash.constData(), "bts_environment_sha256"));
  // Keep a standard World environment node for stock Blender as well as Material Preview.
  if (!scene.world)
    scene.world = BKE_world_add(&main, "BTS Environment");
  if (!scene.world->nodetree)
    bke::node_tree_add_tree_embedded(&main, &scene.world->id, "World Shader", "ShaderNodeTree");
  auto &tree = *scene.world->nodetree;
  bNode *texture = nullptr, *background = nullptr, *output = nullptr;
  for (bNode &node : tree.nodes) {
    if (STREQ(node.idname, "ShaderNodeTexEnvironment") &&
        (STREQ(node.name, "__BTS_Environment") || node.id == &image->id ||
         (previous && node.id == previous)))
      texture = &node;
    if (STREQ(node.idname, "ShaderNodeBackground"))
      background = &node;
    if (STREQ(node.idname, "ShaderNodeOutputWorld") && (!output || (node.flag & NODE_DO_OUTPUT)))
      output = &node;
  }
  if (!texture) {
    texture = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexEnvironment"));
    STRNCPY(texture->name, "__BTS_Environment");
    if (!output)
      output = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputWorld"));
    auto *surface = bke::node_find_enabled_input_socket(*output, "Surface");
    if (surface && surface->link && STREQ(surface->link->fromnode->idname, "ShaderNodeBackground"))
      background = surface->link->fromnode;
    if (!background)
      background = bke::node_add_node(nullptr, tree, UString("ShaderNodeBackground"));
    auto *color = bke::node_find_enabled_input_socket(*background, "Color");
    if (!surface || !color)
      throw std::runtime_error("World environment sockets missing");
    bke::node_remove_socket_links(tree, *color);
    bke::node_add_link(
        tree, *texture, *static_cast<bNodeSocket *>(texture->outputs.first), *background, *color);
    if (!surface->link)
      bke::node_add_link(tree,
                         *background,
                         *static_cast<bNodeSocket *>(background->outputs.first),
                         *output,
                         *surface);
  }
  if (texture->id != &image->id) {
    if (texture->id)
      id_us_min(texture->id);
    texture->id = &image->id;
    id_us_plus(texture->id);
    BKE_ntree_update_tag_node_property(&tree, texture);
  }
  BKE_ntree_update_after_single_tree_change(main, tree);
}
}  // namespace
void pack_project_environment(Main &main, Scene &scene, View3D &view)
{
  for (StudioLight &light : BKE_studiolight_listbase())
    if (STREQ(light.name, view.shading.lookdev_light)) {
      pack_environment_source(main,
                              scene,
                              bool(view.shading.flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION),
                              light.filepath,
                              light.name);
      return;
    }
  throw std::runtime_error("Cannot pack the active Studio Light");
}
namespace {
void read_environment(Scene &scene, PendingProject &pending)
{
  auto *root = IDP_GetProperties(&scene.id);
  auto *ref = property(root, "bts_environment", IDP_ID);
  auto *id = IDP_ID_get(ref);
  if (!id || GS(id->name) != ID_IM)
    throw std::runtime_error("Packed environment reference missing");
  auto *image = reinterpret_cast<Image *>(id);
  auto *packed = static_cast<ImagePackedFile *>(image->packedfiles.first);
  if (!packed || !packed->packedfile || packed->packedfile->size <= 0)
    throw std::runtime_error("Packed environment missing or invalid");
  const QByteArray bytes(static_cast<const char *>(packed->packedfile->data),
                         packed->packedfile->size);
  QString name = "TT_environmentLights_v001.hdr";
  QByteArray expected = "48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031";
  if (IDP_GetPropertyFromGroup(root, "bts_environment_name")) {
    name = QString::fromUtf8(IDP_string_get(property(root, "bts_environment_name", IDP_STRING)));
    expected = IDP_string_get(property(root, "bts_environment_sha256", IDP_STRING));
  }
  if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != expected)
    throw std::runtime_error("Packed environment hash mismatch");
  pending.environment_bytes = bytes;
  pending.environment_name = name;
}
}  // namespace
ProjectFileIdentity write_project(Main &main,
                                  Scene &scene,
                                  const TextureDocument &document,
                                  const std::string &path,
                                  bool update_main_path,
                                  std::optional<ProjectFileIdentity> expected)
{
  if (path.size() >= sizeof(main.filepath) ||
      !QFileInfo(QString::fromStdString(path)).isAbsolute())
    throw std::runtime_error("Save requires an absolute .blend path shorter than 1024 bytes");
  performance::Scope timing("save_project_encode_write");
  const auto document_json = document.to_json();
  const auto metadata = QJsonDocument(document_json).toJson(QJsonDocument::Compact);

  std::vector<TextureSetBinding> bindings;
  for (const auto &set : document.texture_sets())
    bindings.push_back({set.id, set.material_session_uid, set.name});
  TextureDocument::from_json(document_json,
                             bindings);  // Never write metadata this reader would reject.
  auto *sets = group(scene.id, "bts_sets"), *pixels = group(scene.id, "bts_layer_pixels");
  const float zero[4] = {};
  for (const auto &set : document.texture_sets()) {
    Material *material = nullptr;
    for (Material &candidate : main.materials)
      if (candidate.id.session_uid == set.material_session_uid)
        material = &candidate;
    if (!material)
      throw std::runtime_error("Save lost a Texture Set material");
    reference(sets, key(set.id), &material->id);
    for (const auto &layer : set.layers)
      for (int tile : set.tiles)
        for (int c = 0; c < 17; ++c) {
          if (c == 7)
            continue;
          const auto name = key(layer.id, c, tile);
          auto *existing = IDP_GetPropertyFromGroup(pixels, name);
          const auto *author = c == 16 ? &layer.displacement.pixels :
                               c == 5  ? (layer.mask ? &layer.mask->pixels : nullptr) :
                               c == 6  ? &layer.ao_pixels :
                               c >= 8  ? &layer.users[c - 8].pixels :
                                         &layer.pixels;
          const int channel = c >= 5 ? 2 : c;
          if (!author || !author->tile(tile).page_count(channel)) {
            if (existing)
              IDP_FreeFromGroup(pixels, existing);
            continue;
          }
          const int resolution =
              c == 16 ? custom_authoring_resolution(layer.displacement, set.texture_resolution) :
              c >= 8  ? custom_authoring_resolution(layer.users[c - 8], set.texture_resolution) :
                        TextureDocument::authoring_resolution(set, layer, c == 5);
          Image *image = nullptr;
          if (existing) {
            if (existing->type != IDP_ID || !IDP_ID_get(existing) ||
                GS(IDP_ID_get(existing)->name) != ID_IM)
              throw std::runtime_error("Saved author image binding changed");
            image = reinterpret_cast<Image *>(IDP_ID_get(existing));
          }
          else {
            image = BKE_image_add_generated(&main,
                                            resolution,
                                            resolution,
                                            ("BTS Layer " + name).c_str(),
                                            32,
                                            1,
                                            IMA_GENTYPE_BLANK,
                                            zero,
                                            false,
                                            true,
                                            false);
            if (!image)
              throw std::runtime_error("Cannot allocate packed author image");
            image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
            reference(pixels, name, &image->id);
          }
          {
            Buffer held(image);
            auto *ibuf = held.buffer;
            if (!ibuf || ibuf->x != resolution || ibuf->y != resolution || !ibuf->float_data())
              throw std::runtime_error("Author image dimensions changed");
            auto *data = ibuf->float_data_for_write();
            // GPU readback already produced canonical sparse pages. Encoding them
            // needs a row copy, not a map lookup/sample for every empty texel.
            std::fill_n(data, std::size_t(resolution) * resolution * 4, 0.f);
            author->tile(tile).visit_pages(
                [&](int plane, std::uint32_t key, std::span<const PaintPixels::Pixel> values) {
                  if (plane != channel)
                    return;
                  const int x = int(key % 65536) * 64, y = int(key / 65536) * 64;
                  for (int row = 0; row < std::min(64, resolution - y); ++row)
                    std::memcpy(data + (std::size_t(y + row) * resolution + x) * 4,
                                values.data() + row * 64,
                                std::size_t(std::min(64, resolution - x)) *
                                    sizeof(PaintPixels::Pixel));
                });
            ibuf->foptions.flag = (ibuf->foptions.flag & ~OPENEXR_CODEC_MASK) |
                                  R_IMF_EXR_CODEC_ZIP;
            BKE_image_mark_dirty(image, ibuf);
          }
          if (!safe_image_memorypack(image))
            throw std::runtime_error("Cannot pack author image as OpenEXR");
        }
  }
  {
    performance::Scope timing("save_mesh_maps");
    pack_mesh_maps(main, scene, document);
  }
  {
    performance::Scope timing("save_stroke_history");
    pack_stroke_history(main, scene, document);
  }
  {
    performance::Scope timing("save_skew_fields");
    pack_skew_fields(main, scene, document);
  }
  IDP_ReplaceInGroup(
      IDP_EnsureProperties(&scene.id),
      IDP_NewString(StringRef(metadata.constData(), metadata.size()), "bts_document"));
  BlendFileWriteParams params{};
  params.remap_mode = BLO_WRITE_PATH_REMAP_NONE;
  params.use_save_as_copy = true;
  Reports reports;
  performance::Scope file_timing("save_blend_file");
  const auto saved = durable_project_write(
      QString::fromStdString(path), expected, [&](const QString &temporary) {
        if (!BLO_write_file(
                &main, temporary.toUtf8().constData(), G_FILE_COMPRESS, &params, &reports.list))
          throw std::runtime_error("Native .blend write failed; see Blender report");
      });
  if (update_main_path)
    STRNCPY(main.filepath, path.c_str());
  return saved;
}
struct ProjectSaveSnapshot::Impl {
  std::unique_ptr<bke::blendfile::PartialWriteContext> context;
  Scene *scene = nullptr;
  TextureDocument document;
  std::vector<StudioAsset> resources;
  // Keep shared source buffers non-mutable throughout worker encoding, even if
  // their live graph/cache is retired after this snapshot has been requested.
  MaterialLayerSourceSnapshotOwners immutable_source_pixels;
  MaterialLayerSourceBindings layer_source_bindings;
  std::string environment_path, environment_name;
  bool follows_view = true;
  struct Source {
    std::vector<TilePaintPixels *> destinations;
    TilePaintPixels original;
    std::string key;
  };
  struct SourceChunk {
    std::size_t source, index = 0;
    int tile, channel, x, y, width, height;
  };
  std::vector<Source> sources;
  std::map<std::string, std::size_t> source_identities;
  std::vector<SourceChunk> chunks;
  std::size_t next_chunk = 0;
  std::map<const StrokeRaster *, std::shared_ptr<StrokeRaster>> rasters;
  std::map<const StrokeHistory *, std::shared_ptr<StrokeHistory>> histories;
  std::shared_ptr<const StrokeRaster> clone_raster(
      const std::shared_ptr<const StrokeRaster> &source, const std::string &key)
  {
    if (!source)
      return {};
    if (const auto found = rasters.find(source.get()); found != rasters.end())
      return found->second;
    auto copy = std::make_shared<StrokeRaster>(*source);
    rasters[source.get()] = copy;
    add_source(copy->paint, key + "/paint");
    add_source(copy->ao, key + "/ao");
    return copy;
  }
  void clone_history(std::shared_ptr<const StrokeHistory> &source, const std::string &key)
  {
    if (!source || !source->loaded)
      return;
    if (const auto found = histories.find(source.get()); found != histories.end()) {
      source = found->second;
      return;
    }
    auto copy = std::make_shared<StrokeHistory>(*source);
    histories[source.get()] = copy;
    copy->baseline = clone_raster(source->baseline, key + "/baseline");
    copy->final_pixels = clone_raster(source->final_pixels, key + "/final");
    for (std::size_t i = 0; i < copy->operations.size(); ++i) {
      auto operation = std::make_shared<StrokeOperation>(*copy->operations[i]);
      operation->ink = clone_raster(operation->ink, key + "/ink" + std::to_string(i));
      copy->operations[i] = std::move(operation);
    }
    source = std::move(copy);
  }

  void add_source(TilePaintPixels &pixels, const std::string &key)
  {
    if (!std::any_of(pixels.tiles().begin(), pixels.tiles().end(), [](const auto &entry) {
          return bool(entry.second.recipe());
        }))
      return;
    std::string identity;
    const auto append = [&](const auto &value) {
      identity.append(reinterpret_cast<const char *>(&value), sizeof(value));
    };
    for (const auto &[tile, plane] : pixels.tiles()) {
      append(tile);
      append(plane.resolution());
      const auto recipe = reinterpret_cast<std::uintptr_t>(plane.recipe().get());
      append(recipe);
      if (!recipe) {
        const auto versions = plane.page_versions();
        for (int c = 0; c < 5; ++c) {
          append(c);
          const auto count = versions[c].size();
          append(count);
          for (const auto &[page, version] : versions[c]) {
            append(page);
            append(version);
          }
        }
      }
    }
    if (const auto found = source_identities.find(identity); found != source_identities.end()) {
      sources[found->second].destinations.push_back(&pixels);
      return;
    }
    const auto index = sources.size();
    source_identities.emplace(std::move(identity), index);
    sources.push_back({{&pixels}, pixels, key});
    for (const auto &[tile, plane] : pixels.tiles()) {
      const auto versions = plane.page_versions();
      for (int c = 0; c < 5; ++c) {
        std::set<std::pair<int, int>> blocks;
        for (const auto &[page, version] : versions[c])
          blocks.emplace(int(page % 65536) / 4, int(page / 65536) / 4);
        for (const auto &[x, y] : blocks)
          chunks.push_back({index,
                            0,
                            tile,
                            c,
                            x * 256,
                            y * 256,
                            std::min(256, plane.resolution() - x * 256),
                            std::min(256, plane.resolution() - y * 256)});
      }
    }
  }
  wmWindowManager *window_manager = nullptr;
  ~Impl()
  {
    // The shell contains no GHOST windows, event handlers or live UI runtimes.
    // Free its hooks against its own Main: wm_window_free uses G_MAIN, which is
    // expressly forbidden from this persistence worker.
    if (window_manager)
      while (auto *window = static_cast<wmWindow *>(BLI_pophead(&window_manager->windows))) {
        if (window->workspace_hook)
          BKE_workspace_instance_hook_free(&context->bmain, window->workspace_hook);
        MEM_delete(window->stereo3d_format);
        MEM_delete(window);
      }
  }
  Impl(const TextureDocument &doc, const std::vector<StudioAsset> &assets)
      : document(doc), resources(assets)
  {
  }
};
ProjectSaveSnapshot::ProjectSaveSnapshot(Main &main,
                                         Scene &scene,
                                         View3D &view,
                                         const TextureDocument &doc,
                                         const std::vector<StudioAsset> &resources)
    : impl_(std::make_unique<Impl>(doc, resources))
{
  performance::Scope timing("save_snapshot_ids");
  auto &s = *impl_;
  s.layer_source_bindings = capture_material_layer_source_bindings(main, doc);
  s.context = std::make_unique<bke::blendfile::PartialWriteContext>(main);
  s.context->bmain.colorspace = main.colorspace;
  using Context = bke::blendfile::PartialWriteContext;
  s.scene = reinterpret_cast<Scene *>(s.context->id_add(&scene.id, {Context::ADD_DEPENDENCIES}));
  if (!s.scene)
    throw std::runtime_error("Cannot create detached project save snapshot");
  // Preserve an ordinary usable Blender workspace as well as the scene. WM IDs
  // deliberately have no generic Blender copy operation; make a detached shell
  // containing only persistent window fields and cloned workspace dependencies.
  for (WorkSpace &workspace : main.workspaces)
    s.context->id_add(&workspace.id, {Context::ADD_DEPENDENCIES});
  auto &snapshot_main = s.context->bmain;
  auto *source_manager = static_cast<wmWindowManager *>(main.wm.first);
  auto *source_window = source_manager ? static_cast<wmWindow *>(source_manager->windows.first) :
                                         nullptr;
  if (!source_window || !source_window->workspace_hook || !source_window->workspace_hook->active)
    throw std::runtime_error("Project snapshot has no source workspace");
  auto *workspace = reinterpret_cast<WorkSpace *>(
      s.context->id_add(&source_window->workspace_hook->active->id, {Context::ADD_DEPENDENCIES}));
  s.window_manager = reinterpret_cast<wmWindowManager *>(
      BKE_id_new(&snapshot_main, ID_WM, "BTS Snapshot"));
  s.window_manager->runtime = MEM_new<bke::WindowManagerRuntime>(__func__);
  auto *window = MEM_new<wmWindow>(__func__);
  window->winid = source_window->winid;
  window->scene = s.scene;
  window->sizex = source_window->sizex;
  window->sizey = source_window->sizey;
  STRNCPY(window->view_layer_name, source_window->view_layer_name);
  window->stereo3d_format = MEM_new<Stereo3dFormat>(__func__);
  window->workspace_hook = BKE_workspace_instance_hook_create(&snapshot_main, window->winid);
  BKE_workspace_active_set(window->workspace_hook, workspace);
  auto *layout = static_cast<WorkSpaceLayout *>(workspace->layouts.first);
  if (source_window->workspace_hook->act_layout)
    for (WorkSpaceLayout &candidate : workspace->layouts)
      if (candidate.screen->id.session_uid ==
          source_window->workspace_hook->act_layout->screen->id.session_uid)
      {
        layout = &candidate;
        break;
      }
  if (!layout)
    throw std::runtime_error("Project snapshot lost its viewport layout");
  BKE_workspace_active_layout_set(window->workspace_hook, window->winid, workspace, layout);
  BLI_addtail(&s.window_manager->windows, window);
  if (!s.context->is_valid())
    throw std::runtime_error("Detached save snapshot retains live ID references");
  // ID copies deliberately omit Image runtime caches. Transfer immutable map
  // headers now, while source bindings are stable on the owner. Shared pixels
  // survive later map replacement; encoding remains solely on the save worker.
  s.immutable_source_pixels = retain_material_layer_source_snapshot_headers(main, snapshot_main);
  s.follows_view = bool(view.shading.flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION);
  for (StudioLight &light : BKE_studiolight_listbase())
    if (STREQ(light.name, view.shading.lookdev_light)) {
      s.environment_path = light.filepath;
      s.environment_name = light.name;
      break;
    }
  if (s.environment_path.empty())
    throw std::runtime_error("Cannot snapshot the active Studio Light");
  PaintMetadataScope metadata;
  for (const auto &stored : s.document.texture_sets()) {
    auto &set = s.document.set(stored.material_session_uid);
    for (auto &layer : set.layers) {
      const auto key = set.id.toString().toStdString() + layer.id.toString().toStdString();
      s.add_source(layer.pixels, key + "/pixels");
      s.add_source(layer.ao_pixels, key + "/ao");
      s.add_source(layer.displacement.pixels, key + "/displacement");
      s.clone_history(layer.strokes, key + "/history");
      s.clone_history(layer.displacement.strokes, key + "/displacement-history");
      for (int i = 0; i < 8; ++i) {
        s.add_source(layer.users[i].pixels, key + "/user" + std::to_string(i));
        s.clone_history(layer.users[i].strokes, key + "/user-history" + std::to_string(i));
      }
      const auto effect_sources = [&](EffectPaint &paint, const std::string &name) {
        for (int i = 0; i < 11; ++i)
          s.add_source(paint.plane(i), name + "/" + std::to_string(i));
        s.clone_history(paint.strokes, name + "/history");
        s.clone_history(paint.displacement_strokes, name + "/displacement-history");
        for (int i = 0; i < 8; ++i)
          s.clone_history(paint.user_strokes[i], name + "/user-history" + std::to_string(i));
      };
      for (auto &effect : layer.effects)
        effect_sources(effect.paint, key + effect.id.toString().toStdString());
      if (layer.mask) {
        s.add_source(layer.mask->pixels, key + "/mask");
        s.clone_history(layer.mask->strokes, key + "/mask-history");
        for (auto &effect : mask_effects_recursive(layer.mask->effects))
          effect_sources(effect.paint, key + effect.id.toString().toStdString());
      }
    }
  }
}
ProjectSaveSnapshot::~ProjectSaveSnapshot() = default;
bool ProjectSaveSnapshot::capture_authored(MaterialInspection &inspection,
                                           GpuProjectReadback &readback)
{
  auto &s = *impl_;
  if (s.next_chunk == s.chunks.size())
    return true;
  const auto started = performance::Clock::now();
  inspection.begin_project_source_capture();
  GpuPersistenceScope gpu_only;
  PaintMetadataScope metadata;
  while (s.next_chunk < s.chunks.size()) {
    auto &chunk = s.chunks[s.next_chunk];
    const auto &source = s.sources[chunk.source];
    const auto &pixels = source.original.tile(chunk.tile);
    const auto key = "save-author/" + source.key + ":" + std::to_string(chunk.tile) + ":" +
                     std::to_string(chunk.channel);
    chunk.index = readback.images().size();
    if (!inspection.capture_project_source(key,
                                           pixels,
                                           chunk.channel,
                                           chunk.tile,
                                           chunk.x,
                                           chunk.y,
                                           chunk.width,
                                           chunk.height,
                                           readback))
      return false;
    ++s.next_chunk;
    if (performance::ms(started) >= 3)
      return false;
  }
  return true;
}
ProjectFileIdentity ProjectSaveSnapshot::write(const std::string &path,
                                               const GpuProjectReadback &readback,
                                               std::optional<ProjectFileIdentity> expected)
{
  GpuPersistenceScope gpu_only;
  performance::Scope timing("save_snapshot_write");
  auto &s = *impl_;
  auto &main = s.context->bmain;
  // Mapped PBOs are immutable and fence-complete. This worker performs encoding
  // and file I/O only; composition and padding have already happened on the GPU.
  std::map<std::uint32_t, Image *> images;
  for (Image &image : main.images)
    images.emplace(image.id.session_uid, &image);
  // Rebuild canonical sparse authoring from bounded GPU chunks. Never ask a
  // GPU brush recipe to rasterize itself on the CPU while saving.
  std::vector<TilePaintPixels> canonical(s.sources.size());
  std::size_t readback_fixes = 0;
  for (const auto &chunk : s.chunks) {
    const auto &source = s.sources[chunk.source];
    const auto &capture = readback.images().at(chunk.index);
    const auto &original = source.original.tile(chunk.tile);
    const auto versions = original.page_versions();
    for (int y = 0; y < chunk.height; y += 64)
      for (int x = 0; x < chunk.width; x += 64) {
        const auto key = std::uint32_t((chunk.y + y) / 64) * 65536 +
                         std::uint32_t((chunk.x + x) / 64);
        if (!versions[chunk.channel].contains(key))
          continue;
        std::array<PaintPixels::Pixel, 64 * 64> page;
        for (int py = 0; py < 64; ++py)
          for (int px = 0; px < 64; ++px) {
            const auto *pixel = capture.pixels +
                                (capture.uniform ?
                                     0 :
                                     (std::size_t(y + py) * chunk.width + x + px) * 4);
            std::copy_n(pixel, 4, page[py * 64 + px].begin());
            readback_fixes += sanitize_readback_pixel(page[py * 64 + px].data());
          }
        if (std::any_of(page.begin(), page.end(), [](const auto &pixel) { return pixel[3] > 0; }))
          canonical[chunk.source].assign_page(
              original.resolution(), chunk.channel, key, page, chunk.tile);
      }
  }
  if (readback_fixes)
    std::fprintf(stderr, "BTS_SAVE_READBACK_SANITIZED components=%zu\n", readback_fixes);
  for (std::size_t i = 0; i < s.sources.size(); ++i)
    for (auto *destination : s.sources[i].destinations)
      *destination = canonical[i];
  std::set<Image *> updated;
  std::map<std::uint32_t, bool> uniform_images;
  for (const auto &capture : readback.images())
    if (capture.uid) {
      auto [entry, first] = uniform_images.try_emplace(capture.uid, true);
      entry->second = entry->second && capture.uniform;
    }
  for (const auto &capture : readback.images()) {
    if (!capture.uid)
      continue;
    const auto found = images.find(capture.uid);
    if (found == images.end())
      throw std::runtime_error("Snapshot lost a GPU image ID");
    auto *image = found->second;
    if (uniform_images.at(capture.uid)) {
      auto *tile = BKE_image_get_tile(image, capture.tile);
      if (!tile)
        throw std::runtime_error("Uniform GPU snapshot lost its tile");
      BKE_image_free_packedfiles(image);
      image->source = (image->source == IMA_SRC_TILED || image->tiles.first != image->tiles.last ||
                       (image->tiles.first &&
                        static_cast<ImageTile *>(image->tiles.first)->tile_number != 1001)) ?
                          IMA_SRC_TILED :
                          IMA_SRC_GENERATED;
      image->type = image->source == IMA_SRC_TILED ? IMA_TYPE_IMAGE : IMA_TYPE_UV_TEST;
      // These derived proxies already contain working-space linear numbers. A
      // Non-Color generated image avoids Blender's generated-color sRGB decoding,
      // preserves float bits/HDR/signed data and feeds stock shader nodes unchanged.
      STRNCPY(image->colorspace_settings.name, "Non-Color");
      // Disabled native world-normal inputs use a 1x1 zero GPU placeholder after
      // Open. Its readback is uniform, but the saved descriptor must retain the
      // texture set's logical dimensions or the next Open rejects the project.
      if (!IDP_GetPropertyFromGroup_null(image->id.properties, "bts_native_world_normal_owner")) {
        tile->gen_x = capture.width;
        tile->gen_y = capture.height;
      }
      tile->gen_depth = 32;
      tile->gen_type = IMA_GENTYPE_BLANK;
      tile->gen_flag |= IMA_GEN_FLOAT | IMA_GEN_TILE;
      std::copy_n(capture.pixels, 4, tile->gen_color);
      continue;
    }
    ImageUser user{};
    user.tile = capture.tile;
    auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
    if (!buffer || !buffer->float_data() || buffer->x != capture.width ||
        buffer->y != capture.height)
    {
      if (buffer)
        BKE_image_release_ibuf(image, buffer, nullptr);
      throw std::runtime_error("Snapshot image dimensions differ from GPU readback");
    }
    if (capture.uniform) {
      auto *destination = buffer->float_data_for_write();
      for (std::size_t i = 0; i < std::size_t(capture.width) * capture.height; ++i)
        std::copy_n(capture.pixels, 4, destination + i * 4);
    }
    else
      std::copy_n(capture.pixels,
                  std::size_t(capture.width) * capture.height * 4,
                  buffer->float_data_for_write());
    buffer->foptions.flag = (buffer->foptions.flag & ~OPENEXR_CODEC_MASK) | R_IMF_EXR_CODEC_ZIP;
    BKE_image_mark_dirty(image, buffer);
    BKE_image_release_ibuf(image, buffer, nullptr);
    updated.insert(image);
  }
  for (auto *image : updated)
    if (!safe_image_memorypack(image))
      throw std::runtime_error("Cannot pack GPU project snapshot image");
  // Pack the original encoding once. Only its raw carrier Image is serialized
  // for the new representation; typed canonical aliases stay runtime-only.
  BKE_main_id_refcount_recompute(&main, false);
  ProjectResourceImages resource_images;
  {
    performance::Scope timing("save_resource_pack");
    pack_project_assets(main, *s.scene, s.resources, &resource_images, s.context.get());
  }
  pack_material_layer_source_bindings(
      *s.context, *s.scene, s.layer_source_bindings, resource_images);
  if (s.layer_source_bindings.empty())
    pack_material_layer_source_snapshots(main);
  prune_unused_material_resource_copies(*s.context, resource_images);
  // Persist material appearance, independent of the active inspection overlay.
  for (Material &material : main.materials)
    if (material.nodetree) {
      auto &tree = *material.nodetree;
      for (const char *name : {"__BTS_ViewSwitch", "__BTS_GeometryEnabled"})
        if (auto *node = bke::node_find_node_by_name(tree, name))
          if (auto *socket = static_cast<bNodeSocket *>(
                  BLI_findlink(&node->inputs, STREQ(name, "__BTS_GeometryEnabled") ? 1 : 0)))
            if (socket->default_value)
              static_cast<bNodeSocketValueFloat *>(socket->default_value)->value = 0;
    }
  // Inspection outputs do not participate in stock material shading; preserve
  // their native generated descriptors instead of old, large packed caches.
  for (Image &image : main.images)
    if (std::string_view(image.id.name + 2).starts_with("BTS Authored Normal Inspection") ||
        std::string_view(image.id.name + 2).starts_with("BTS Mask "))
    {
      BKE_image_free_packedfiles(&image);
      image.source = (image.source == IMA_SRC_TILED || image.tiles.first != image.tiles.last ||
                      (image.tiles.first &&
                       static_cast<ImageTile *>(image.tiles.first)->tile_number != 1001)) ?
                         IMA_SRC_TILED :
                         IMA_SRC_GENERATED;
      image.type = image.source == IMA_SRC_TILED ? IMA_TYPE_IMAGE : IMA_TYPE_UV_TEST;
      for (ImageTile &tile : image.tiles)
        tile.gen_flag |= IMA_GEN_FLOAT | IMA_GEN_TILE;
    }
  pack_environment_source(main, *s.scene, s.follows_view, s.environment_path, s.environment_name);
  // The detached dependency copy carries source ID counts. Recompute its own
  // references before retiring replaced private resources, so an orphan cannot
  // survive merely because the source Main had an extra user.
  BKE_main_id_refcount_recompute(&main, false);

  DisplacementPreview::SaveScope preview(main);
  return write_project(main, *s.scene, s.document, path, false, expected);
}
PendingProject::~PendingProject()
{
  validated_inspection.reset();
  native_sources.clear();
  native_object_contexts.clear();
  if (file)
    BLO_blendfiledata_free(file);
}
void PendingProject::validate() const
{
  if (failure)
    std::rethrow_exception(failure);
  if (!file || !document)
    throw std::logic_error("Project read returned no validated document");
}
void PendingProject::activate_environment()
{
  validate();
  register_environment_bytes(environment_bytes, environment_name);
}
std::unique_ptr<PendingProject> PendingProject::read(const std::string &path)
{
  auto pending = prepare(path);
  pending->validate();
  return pending;
}
std::unique_ptr<PendingProject> PendingProject::prepare(
    const std::string &path,
    const std::shared_ptr<std::atomic_bool> &cancel,
    const std::function<void(const QString &)> &progress)
{
  auto pending = std::make_unique<PendingProject>();
  const auto stage = [&](const char *text) {
    if (cancel && cancel->load())
      throw std::runtime_error("Project Open cancelled");
    if (progress)
      progress(QString::fromUtf8(text));
  };
  try {
    stage("Reading native project…");
    pending->disk_identity = ProjectFileIdentity::read(QString::fromStdString(path));
    Reports reports;
    BlendFileReadParams params{};
    params.skip_flags = BLO_READ_SKIP_USERDEF;
    BlendFileReadReport report{};
    report.reports = &reports.list;
    std::unique_ptr<BlendFileData, decltype(&BLO_blendfiledata_free)> file(
        BKE_blendfile_read(path.c_str(), &params, &report), BLO_blendfiledata_free);
    pending->file = file.release();
    require_project_identity(QString::fromStdString(path), pending->disk_identity);
    if (!pending->file || !pending->file->main || !pending->file->curscene)
      throw std::runtime_error("Cannot read native .blend project");
    auto *data = pending->file;
    stage("Validating project metadata…");
    auto &main = *pending->file->main;
    auto &scene = *pending->file->curscene;
    if (!main.libraries.is_empty())
      throw std::runtime_error("Linked project dependencies are not supported yet");
    bool viewport = false;
    if (data->curscreen)
      for (ScrArea &area : data->curscreen->areabase)
        if (area.spacetype == SPACE_VIEW3D)
          for (ARegion &region : area.regionbase)
            if (region.regiontype == RGN_TYPE_WINDOW && region.regiondata)
              viewport = true;
    if (!viewport || main.wm.is_empty())
      throw std::runtime_error("Saved project has no usable native viewport");
    auto *root = IDP_GetProperties(&scene.id);
    auto *json = property(root, "bts_document", IDP_STRING);
    // Explicit project setting wins. Older files retain each saved native View3D flag.
    if (auto *follow = IDP_GetPropertyFromGroup(root, "bts_lighting_follows_view")) {
      if (follow->type != IDP_BOOLEAN)
        throw std::runtime_error("Invalid project lighting-follow setting");
      for (bScreen &screen : main.screens)
        for (ScrArea &area : screen.areabase)
          for (SpaceLink &space : area.spacedata)
            if (space.spacetype == SPACE_VIEW3D) {
              auto &view = *reinterpret_cast<View3D *>(&space);
              if (IDP_bool_get(follow))
                view.shading.flag |= V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
              else
                view.shading.flag &= ~V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
            }
    }
    if (json->len < 1)
      throw std::runtime_error("Project metadata is empty");
    display_post_settings(scene);
    subsurface_preview(scene);
    QJsonParseError error;
    const auto metadata = QJsonDocument::fromJson(QByteArray(IDP_string_get(json)), &error);
    if (error.error != QJsonParseError::NoError || !metadata.isObject())
      throw std::runtime_error("Invalid project JSON");
    auto *sets = property(root, "bts_sets", IDP_GROUP),
         *pixels = property(root, "bts_layer_pixels", IDP_GROUP);
    std::vector<TextureSetBinding> bindings;
    for (const auto &entry : metadata.object()["texture_sets"].toArray()) {
      const QUuid id(entry.toObject()["id"].toString());
      auto *ref = property(sets, key(id).c_str(), IDP_ID);
      auto *mat = IDP_ID_get(ref);
      if (!mat || GS(mat->name) != ID_MA)
        throw std::runtime_error("Texture Set material reference is missing");
      bindings.push_back({id, mat->session_uid, mat->name + 2});
    }
    std::map<std::string, QJsonObject> ao_metadata, user_metadata;
    for (const auto &set : metadata.object()["texture_sets"].toArray())
      for (const auto &layer : set.toObject()["layers"].toArray()) {
        const auto l = layer.toObject();
        if (metadata.object()["schema"].toInt() >= 47)
          for (const auto &entry : l["displacement"].toObject()["tiles"].toArray()) {
            const auto tile = entry.toObject();
            const auto name = key(QUuid(l["id"].toString()), 16, tile["number"].toInt());
            property(pixels, name.c_str(), IDP_ID);
            user_metadata[name] = tile;
          }
        if (metadata.object()["schema"].toInt() >= 26) {
          const auto users = l["users"].toArray();
          if (users.size() != custom_channel_count)
            throw std::runtime_error("Custom author metadata is missing");
          for (int slot = 0; slot < custom_channel_count; ++slot)
            for (const auto &entry : users[slot].toObject()["tiles"].toArray()) {
              const auto tile = entry.toObject();
              const auto name = key(QUuid(l["id"].toString()), 8 + slot, tile["number"].toInt());
              property(pixels, name.c_str(), IDP_ID);
              user_metadata[name] = tile;
            }
        }
        if (metadata.object()["schema"].toInt() >= 14)
          for (const auto &entry : l["ao"].toObject()["tiles"].toArray()) {
            const auto tile = entry.toObject();
            const auto name = key(QUuid(l["id"].toString()), 6, tile["number"].toInt());
            property(pixels, name.c_str(), IDP_ID);
            ao_metadata[name] = tile;
          }
        for (const auto &tile : l["mask"].toObject()["tiles"].toArray()) {
          const auto t = tile.toObject();
          if (t["pages"].toInteger() > 0)
            property(
                pixels, key(QUuid(l["id"].toString()), 5, t["number"].toInt()).c_str(), IDP_ID);
        }
        QJsonArray tiles = l["paint_tiles"].toArray();
        if (metadata.object()["schema"].toInt() == 1)
          tiles = {QJsonObject{{"number", 1001}, {"pages", l["paint_pages"]}}};
        for (const auto &tile : tiles) {
          const auto t = tile.toObject();
          const auto pages = t["pages"].toArray();
          if (pages.size() != 5)
            throw std::runtime_error("Saved paint presence metadata is missing");
          for (int c = 0; c < 5; ++c)
            if (pages[c].toInteger() > 0)
              property(
                  pixels, key(QUuid(l["id"].toString()), c, t["number"].toInt()).c_str(), IDP_ID);
        }
      }
    auto document = TextureDocument::from_json(metadata.object(), bindings);
    // Before schema 73 editability was not serialized. Recognize our explicitly
    // deferred image profiles without weakening validation of editable BTS graphs.
    if (metadata.object()["schema"].toInt() < 73)
      for (Material &material : main.materials) {
        auto *properties = IDP_GetProperties(&material.id);
        auto *source = properties ? IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr") :
                                    nullptr;
        if (!source || source->type != IDP_STRING)
          continue;
        const auto profile = QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();
        if (profile["converted"].toBool() || profile["images"].toObject().isEmpty() ||
            (material.nodetree &&
             bke::node_find_node_by_name(*material.nodetree, "__BTS_ViewSwitch")))
          continue;
        for (const auto &binding : bindings)
          if (binding.material_uid == material.id.session_uid)
            document.set(binding.material_uid).uniform_editable = false;
      }

    for (const auto &binding : bindings)
      for (auto &layer : document.set(binding.material_uid).layers)
        for (int tile : document.set(binding.material_uid).tiles)
          for (int c = 0; c < 17; ++c) {
            if (c == 7)
              continue;
            auto *ref = IDP_GetPropertyFromGroup(pixels, key(layer.id, c, tile));
            if (!ref)
              continue;
            const int resolution = c == 16 ?
                                       custom_authoring_resolution(
                                           layer.displacement,
                                           document.set(binding.material_uid).texture_resolution) :
                                   c >= 8 ?
                                       custom_authoring_resolution(
                                           layer.users[c - 8],
                                           document.set(binding.material_uid).texture_resolution) :
                                       TextureDocument::authoring_resolution(
                                           document.set(binding.material_uid), layer, c == 5);
            if ((c == 5 ? !layer.mask.has_value() : layer.kind != LayerKind::Paint) ||
                ref->type != IDP_ID || !IDP_ID_get(ref) || GS(IDP_ID_get(ref)->name) != ID_IM)
              throw std::runtime_error("Invalid author image reference");
            auto *image = reinterpret_cast<Image *>(IDP_ID_get(ref));
            if (image->packedfiles.is_empty())
              throw std::runtime_error("Author image is not packed into the project");
            Buffer held(image);
            auto *ibuf = held.buffer;
            if (!ibuf || ibuf->x != resolution || ibuf->y != resolution || ibuf->channels != 4 ||
                !ibuf->float_data())
              throw std::runtime_error("Packed author image has invalid dimensions or format");
            if (c >= 8) {
              const auto *data = ibuf->float_data();
              const auto found = user_metadata.find(key(layer.id, c, tile));
              if (found == user_metadata.end())
                throw std::runtime_error("Custom author metadata is missing");
              for (std::size_t i = 0; i < std::size_t(resolution) * resolution; ++i)
                if (data[i * 4 + 1] != 0 || data[i * 4 + 2] != 0)
                  throw std::runtime_error("Custom scalar author image is invalid");
              auto &user = c == 16 ? layer.displacement : layer.users[c - 8];
              user.pixels.assign_channel(
                  resolution, 2, {data, std::size_t(resolution) * resolution * 4}, tile);
              if (QString::number(user.pixels.tile(tile).hash(2), 16) !=
                      found->second["hash"].toString() ||
                  qint64(user.pixels.tile(tile).page_count(2)) !=
                      found->second["pages"].toInteger())
                throw std::runtime_error("Custom author data does not match its metadata");
            }
            else if (c >= 5) {
              const auto *data = ibuf->float_data();
              for (std::size_t i = 0; i < std::size_t(resolution) * resolution; ++i)
                if (data[i * 4] < 0 || data[i * 4] > data[i * 4 + 3] || data[i * 4 + 1] != 0 ||
                    data[i * 4 + 2] != 0)
                  throw std::runtime_error(
                      "Mask author image contains invalid premultiplied grayscale");
              if (c == 5)
                layer.mask->pixels.assign_channel(
                    resolution, 2, {data, std::size_t(resolution) * resolution * 4}, tile);
              else {
                const auto found = ao_metadata.find(key(layer.id, 6, tile));
                if (found == ao_metadata.end())
                  throw std::runtime_error("AO author metadata is missing");
                layer.ao_pixels.assign_channel(
                    resolution, 2, {data, std::size_t(resolution) * resolution * 4}, tile);
                if (QString::number(layer.ao_pixels.tile(tile).hash(2), 16) !=
                        found->second["hash"].toString() ||
                    qint64(layer.ao_pixels.tile(tile).page_count(2)) !=
                        found->second["pages"].toInteger())
                  throw std::runtime_error("AO author pixels do not match their metadata");
              }
            }
            else
              layer.pixels.assign_channel(
                  resolution,
                  c,
                  {ibuf->float_data(), std::size_t(resolution) * resolution * 4},
                  tile);
          }
    read_mesh_maps(scene, document);
    read_stroke_history(scene, document, metadata.object()["schema"].toInt() >= 16);
    read_skew_fields(scene, document, metadata.object()["schema"].toInt() >= 57);
    stage("Restoring packed project resources…");
    ProjectResourceImages resource_images;
    auto resources = read_project_assets(scene, &resource_images);
    stage("Resolving source texture bindings…");
    resolve_fill_textures(document, resources);
    pending->layer_sources = validate_packed_material_layer_sources(
        main, scene, document, resource_images);
    stage("Validating native material graph…");
    if (native_height_project(document)) {
      if (main.versionfile != BLENDER_FILE_VERSION ||
          main.subversionfile != BLENDER_FILE_SUBVERSION ||
          metadata.object()["schema"] != texture_document_schema)
        throw std::invalid_argument(
            "Raw authored Height Open currently requires a project saved by this host version");
      stage("Validating packed authored Height sources…");
      const std::atomic_bool never_cancelled = false;
      pending->native_sources = prepare_native_height_project(
          document, resources, cancel ? *cancel : never_cancelled);
      if (!data->cur_view_layer)
        throw std::invalid_argument("Raw authored Height project has no recorded view layer");
      std::set<std::uint32_t> materials;
      for (const auto &set : document.texture_sets())
        if (!set.disabled && set.uniform_editable)
          for (const auto &layer : evaluation_layers(set))
            if (layer.fill.native_material)
              materials.insert(set.material_session_uid);
      stage("Validating private native Height object context…");
      pending->native_object_contexts = prepare_native_height_context(
          main, scene, *data->cur_view_layer, materials, cancel ? *cancel : never_cancelled);
    }
    if (native_height_project(document) || pending->layer_sources) {
      pending->validated_inspection = std::make_unique<MaterialInspection>();
      pending->validated_inspection->restore(main, document);
      if (pending->layer_sources)
        pending->validated_inspection->validate_material_layer_sources(document);
      pending->validated_inspection->set_subsurface_enabled(main,
                                                            subsurface_preview(scene).enabled);
    }
    else {
      MaterialInspection validation;
      validation.restore(main, document);
    }
    read_project_symmetry(scene);
    read_environment(scene, *pending);
    stage("Ready to open");
    pending->document.emplace(std::move(document));
    pending->resources = std::move(resources);
  }
  catch (...) {
    pending->failure = std::current_exception();
  }
  return pending;
}
}  // namespace bts
