// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialImages.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeMaterialImageSource.hh"
#include "BLI_boxpack_2d.h"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialFragmentImages.hh"
#include "NativeFragmentGeometry.hh"
#include "BKE_node_runtime.hh"
#include "DNA_ID.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialDecodedImage.hh"
#include "TextureMemory.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "DNA_node_types.h"
#include "BLI_string.h"
#include <set>
#include <algorithm>
#include <cmath>
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "IMB_imbuf.hh"
#include "GPU_texture.hh"
#include "IMB_colormanagement.hh"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include <QJsonObject>
#include <QScopeGuard>
namespace bts {
namespace {
struct NativeTiledPack {
  blender::FixedSizeBoxPack box{};
  std::size_t index = 0;
  float score = 0;
};
int native_tiled_pack_compare(const void *a, const void *b)
{
  return static_cast<const NativeTiledPack *>(a)->score <
         static_cast<const NativeTiledPack *>(b)->score;
}
}  // namespace
std::vector<NativeImageUploadRequest> native_material_image_upload_requests(
    const NativeMaterialGraph &graph,
    std::span<blender::bNodeTree *const> extra_roots,
    bool ordinary_channels)
{
  using namespace blender;
  std::set<const bNode *> visited;
  std::set<Image *> required;
  std::map<Image *, unsigned int> tiled;
  const auto visit = [&](auto &&self, const bNode &node) -> void {
    if (!visited.insert(&node).second)
      return;
    if (STREQ(node.idname, "ShaderNodeTexImage") && node.id && GS(node.id->name) == ID_IM) {
      auto *image = reinterpret_cast<Image *>(node.id);
      required.insert(image);
      if (image->source == IMA_SRC_TILED) {
        const auto *settings = static_cast<const NodeTexImage *>(node.storage);
        if (!settings || !native_tiled_default_user_tile(settings->iuser.tile) ||
            settings->iuser.scene || settings->iuser.pass || settings->iuser.layer ||
            settings->iuser.multi_index || settings->iuser.view || settings->iuser.multiview_eye)
          throw std::invalid_argument(
              "Retained TILED upload requires default ImageUser tile/pass/layer/view");
        if (settings->projection < SHD_PROJ_FLAT || settings->projection > SHD_PROJ_TUBE)
          throw std::invalid_argument("Unknown retained TILED projection");
        tiled[image] |= settings->projection == SHD_PROJ_FLAT ? 1u : 2u;
      }
    }
    for (const auto &input : node.inputs)
      if (input.link && input.link->fromnode)
        self(self, *input.link->fromnode);
  };
  if (ordinary_channels)
    for (const auto &channel : graph.channels)
      if (channel.tree)
        for (const auto &node : channel.tree->nodes)
          if (STREQ(node.idname, "ShaderNodeOutputAOV"))
            visit(visit, node);
  for (const auto *tree : extra_roots)
    if (tree)
      for (const auto &node : tree->nodes)
        if (STREQ(node.idname, "ShaderNodeOutputAOV"))
          visit(visit, node);
  std::vector<NativeImageUploadRequest> result;
  for (auto &image : graph.main->images)
    if (required.contains(&image)) {
      if (image.source == IMA_SRC_TILED) {
        if (tiled.at(&image) & 1u)
          result.push_back({&image, NativeTiledRepresentation::ArrayMapping, true});
        if (tiled.at(&image) & 2u)
          result.push_back({&image, NativeTiledRepresentation::FirstTile2D, true});
      }
      else
        result.push_back({&image});
    }
  return result;
}
std::uint64_t native_tiled_mip_texels(int width, int height)
{
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Native tiled mip dimensions must be positive");
  std::uint64_t total = 0;
  for (;;) {
    total = resource_sum(total, resource_product(width, height));
    if (width == 1 && height == 1)
      return total;
    width = std::max(1, width / 2);
    height = std::max(1, height / 2);
  }
}
NativeTiledPacketView native_tiled_packet_view(const NativeMaterialImageTileSet &source,
                                               const NativeTiledUploadAction &action)
{
  using namespace blender;
  if (action.phase != NativeTiledPhase::Pixels)
    throw std::invalid_argument("Native tiled packet is not a pixel transfer");
  const auto found = source.tiles().find(action.tile);
  if (found == source.tiles().end() || !found->second)
    throw std::invalid_argument("Native tiled packet has no retained source tile");
  const auto *buffer = found->second->buffer();
  if (!buffer || !buffer->float_data() || buffer->channels != 4 ||
      !flag_is_set(buffer->flags, ImBufFlags::AlphaChannelPacked) || action.source_x < 0 ||
      action.source_y < 0 || action.width <= 0 || action.height <= 0 || action.width > buffer->x ||
      action.height > buffer->y || action.source_x > buffer->x - action.width ||
      action.source_y > buffer->y - action.height)
    throw std::invalid_argument(
        "Native tiled packet differs from normalized retained RGBA storage");
  return {buffer->float_data() + 4 * (std::size_t(action.source_y) * buffer->x + action.source_x),
          buffer->x,
          action.width,
          action.height};
}
NativeTiledStoragePlan native_tiled_storage_plan(const blender::Image &image,
                                                 const NativeMaterialImageTileSet &source,
                                                 NativeTiledRepresentation representation,
                                                 NativeTiledCaps caps)
{
  using namespace blender;
  const auto numbers = native_material_tiled_image_numbers(image);
  if (caps.texture_size <= 0 || source.tiles().size() != numbers.size() ||
      (representation == NativeTiledRepresentation::ArrayMapping &&
       (caps.array_layers < 2 || caps.map_width <= 0)))
    throw std::invalid_argument(
        "Native tiled source has invalid capabilities or incomplete retained storage");
  NativeTiledStoragePlan result;
  result.representation = representation;
  result.image_space = QString::fromUtf8(image.colorspace_settings.name);
  result.image_alpha = int(image.alpha_mode);
  result.high_precision = bool(image.flag & IMA_HIGH_BITDEPTH);
  for (const auto &tile : image.tiles)
    result.source_order.push_back(tile.tile_number);
  result.first_tile = result.source_order.front();
  bool first = true;
  for (int number : result.source_order) {
    if (representation == NativeTiledRepresentation::FirstTile2D && number != result.first_tile)
      continue;
    const auto found = source.tiles().find(number);
    if (found == source.tiles().end() || !found->second)
      throw std::invalid_argument("Native tiled plan has no exact retained tile owner");
    const auto *buffer = found->second->buffer();
    if (!buffer || !buffer->float_data() || buffer->channels != 4 ||
        buffer->color_mode != ImColorMode::RGBA ||
        !flag_is_set(buffer->flags, ImBufFlags::AlphaChannelPacked) || buffer->x <= 0 ||
        buffer->y <= 0)
      throw std::invalid_argument(
          "Native tiled upload requires normalized retained float RGBA pixels");
    const auto format = IMB_gpu_get_texture_format(
        buffer, bool(image.flag & IMA_HIGH_BITDEPTH), true);
    NativeTiledFormat typed;
    if (format == gpu::TextureFormat::SFLOAT_32_32_32_32)
      typed = NativeTiledFormat::Float32Rgba;
    else if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
      typed = NativeTiledFormat::Float16Rgba;
    else
      throw std::invalid_argument(
          "Native tiled upload has unsupported normalized storage precision");
    if (first) {
      result.format = typed;
      first = false;
    }
    else if (result.format != typed)
      throw std::invalid_argument(
          "Native tiled upload requires one exact format across all retained tiles");
    if (buffer->x > caps.texture_size || buffer->y > caps.texture_size)
      throw std::length_error(
          "Native tiled full-resolution source exceeds GPU texture size; resampling is not admitted");
    result.width = std::max(result.width, buffer->x);
    result.height = std::max(result.height, buffer->y);
    result.regions.push_back({number, 0, 0, buffer->x, buffer->y, 0});
  }
  if (representation == NativeTiledRepresentation::ArrayMapping) {
    // Exact stock image_gpu.cc score, stable list sort and fixed-area packer.
    // Work on private nodes; do not BKE_image_sort_tiles or edit Image runtime yet.
    std::vector<NativeTiledPack> nodes;
    nodes.reserve(result.regions.size());
    ListBaseT<FixedSizeBoxPack> boxes{};
    for (std::size_t i = 0; i < result.regions.size(); ++i) {
      const auto &region = result.regions[i];
      NativeTiledPack node;
      node.box.w = region.width;
      node.box.h = region.height;
      node.index = i;
      const float width = region.width, height = region.height;
      node.score = std::max(width, height) / std::min(width, height) * width * height;
      nodes.push_back(node);
      BLI_addtail(&boxes, &nodes.back().box);
    }
    BLI_listbase_sort(&boxes, native_tiled_pack_compare);
    while (!boxes.is_empty()) {
      if (result.layers == caps.array_layers)
        throw std::length_error("Native tiled packed layers exceed GPU array capability");
      ListBaseT<FixedSizeBoxPack> packed{};
      BLI_box_pack_2d_fixedarea(&boxes, result.width, result.height, &packed);
      if (packed.is_empty())
        throw std::logic_error("Native tiled stock packer made no progress");
      for (const auto &box : packed) {
        const auto &node = *reinterpret_cast<const NativeTiledPack *>(&box);
        auto &region = result.regions[node.index];
        region.x = box.x;
        region.y = box.y;
        region.layer = result.layers;
      }
      ++result.layers;
    }
    result.map_width = numbers.back() - 1000;
    if (result.map_width > caps.map_width)
      throw std::length_error("Native tiled mapping exceeds GPU texture size");
    result.mapping.resize(resource_product(result.map_width, 2));
    for (int i = 0; i < result.map_width; ++i)
      result.mapping[i][0] = -1.f;
    for (const auto &region : result.regions) {
      const int i = region.tile - 1001;
      result.mapping[i][0] = float(region.layer);
      result.mapping[result.map_width + i] = {float(region.x) / result.width,
                                              float(region.y) / result.height,
                                              float(region.width) / result.width,
                                              float(region.height) / result.height};
    }
    result.mapping_bytes = resource_product(result.map_width, 32);
  }
  else if (representation == NativeTiledRepresentation::FirstTile2D)
    result.layers = 1;
  else
    throw std::invalid_argument("Unknown native tiled representation");
  result.texture_bytes = resource_product(
      resource_product(native_tiled_mip_texels(result.width, result.height), result.layers),
      result.format == NativeTiledFormat::Float32Rgba ? 16 : 8);
  auto ordered = result.regions;
  std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
    return a.tile < b.tile;
  });
  QJsonArray regions;
  for (const auto &r : ordered)
    regions.append(QJsonArray{r.tile, r.x, r.y, r.width, r.height, r.layer});
  const auto source_key = representation == NativeTiledRepresentation::FirstTile2D ?
                              source.tiles().at(result.first_tile)->key() :
                              source.key();
  result.source_key = source_key;
  const auto key = QJsonDocument(QJsonArray{"retained-native-tiled-storage",
                                            source_key,
                                            int(representation),
                                            int(result.format),
                                            result.width,
                                            result.height,
                                            result.layers,
                                            result.map_width,
                                            regions})
                       .toJson(QJsonDocument::Compact);
  result.key = QString::fromLatin1(
      QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
  return result;
}
bool native_tiled_storage_source_valid(const blender::Image &image,
                                       const NativeMaterialImageTileSet &source,
                                       const NativeTiledStoragePlan &plan)
{
  using namespace blender;
  if (image.source != IMA_SRC_TILED || image.type != IMA_TYPE_IMAGE ||
      image.flag & IMA_USE_VIEWS ||
      plan.image_space != QString::fromUtf8(image.colorspace_settings.name) ||
      plan.image_alpha != int(image.alpha_mode) ||
      plan.high_precision != bool(image.flag & IMA_HIGH_BITDEPTH))
    return false;
  if (plan.representation == NativeTiledRepresentation::FirstTile2D) {
    if (image.tiles.is_empty() ||
        static_cast<const ImageTile *>(image.tiles.first)->tile_number != plan.first_tile)
      return false;
    const auto found = source.tiles().find(plan.first_tile);
    return found != source.tiles().end() && found->second &&
           plan.source_key == found->second->key();
  }
  std::vector<int> order;
  for (const auto &tile : image.tiles)
    order.push_back(tile.tile_number);
  return order == plan.source_order && plan.source_key == source.key();
}
QJsonArray prepare_native_material_images(NativeMaterialGraph &graph,
                                          const std::atomic_bool &cancel,
                                          std::span<blender::bNodeTree *const> extra_roots,
                                          bool ordinary_channels)
{
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread())
    throw std::logic_error("Native material images must be decoded on a worker");
  using namespace blender;
  auto tiled_candidates = graph.tiled_image_resources;
  QJsonArray results;
  std::set<Image *> required;
  std::set<const bNode *> visited;
  const auto visit = [&](auto &&self, const bNode &node) -> void {
    if (!visited.insert(&node).second)
      return;
    if (STREQ(node.idname, "ShaderNodeTexImage") && node.id && GS(node.id->name) == ID_IM)
      required.insert(reinterpret_cast<Image *>(node.id));
    for (const auto &socket : node.inputs)
      if (socket.link && socket.link->fromnode)
        self(self, *socket.link->fromnode);
  };
  if (ordinary_channels)
    for (const auto &channel : graph.channels)
      if (channel.tree)
        for (const auto &node : channel.tree->nodes)
          if (STREQ(node.idname, "ShaderNodeOutputAOV"))
            visit(visit, node);
  for (const auto *tree : extra_roots)
    if (tree)
      for (const auto &node : tree->nodes)
        if (STREQ(node.idname, "ShaderNodeOutputAOV"))
          visit(visit, node);

  for (auto &image : graph.main->images) {
    if (!required.contains(&image))
      continue;
    if (cancel.load())
      throw std::runtime_error("Native material image loading cancelled");
    if (image.source != IMA_SRC_FILE && image.source != IMA_SRC_TILED &&
        image.source != IMA_SRC_GENERATED)
      throw std::runtime_error(std::string(image.id.name + 2) +
                               ": native material movie/sequence/render images are not supported");
    if (image.source == IMA_SRC_TILED) {
      QElapsedTimer timer;
      timer.start();
      bool reused = false;
      auto inventory = prepare_shared_native_tiled_image(*graph.main, image, cancel, reused);
      const auto elapsed = timer.nsecsElapsed() / 1e6;
      const auto found = std::find_if(tiled_candidates.begin(),
                                      tiled_candidates.end(),
                                      [&](const auto &entry) { return entry.first == &image; });
      if (found == tiled_candidates.end())
        tiled_candidates.emplace_back(&image, inventory);
      else
        found->second = inventory;
      for (const auto &[number, owner] : inventory->tiles()) {
        const auto *buffer = owner->buffer();
        results.append(QJsonObject{
            {"image", QString::fromUtf8(image.id.name + 2)},
            {"tile", number},
            {"width", buffer->x},
            {"height", buffer->y},
            {"float", buffer->float_data() != nullptr},
            {"decode_ms", elapsed},
            {"shared_decoded_source", reused},
            {"explicit_graph_space_decode", true},
            {"normalization_policy_version", native_material_decode_policy_version},
            {"graph_recorded_space", QString::fromUtf8(graph.main->colorspace.scene_linear_name)},
            {"explicit_float_space",
             buffer->float_buffer.colorspace ?
                 QString::fromUtf8(
                     IMB_colormanagement_colorspace_get_name(buffer->float_buffer.colorspace)) :
                 QString{}},
            {"source_space_override", QString::fromUtf8(image.colorspace_settings.name)}});
      }
      continue;
    }
    for (const auto &tile : image.tiles) {
      if (cancel.load())
        throw std::runtime_error("Native material image loading cancelled");
      QElapsedTimer timer;
      timer.start();
      ImageUser user{};
      BKE_imageuser_default(&user);
      user.tile = tile.tile_number;
      bool reused = false;
      if (image.source == IMA_SRC_FILE && image.type == IMA_TYPE_IMAGE) {
        const auto found = std::find_if(graph.image_resources.begin(),
                                        graph.image_resources.end(),
                                        [&](const auto &entry) { return entry.first == &image; });
        if (found == graph.image_resources.end()) {
          auto resource = prepare_shared_native_image(*graph.main, image, cancel, reused);
          graph.image_resources.emplace_back(&image, std::move(resource));
        }
        else
          reused = true;
      }
      auto *buffer = BKE_image_acquire_ibuf(&image, &user, nullptr);
      if (!buffer)
        throw std::runtime_error(std::string(image.id.name + 2) +
                                 ": cannot decode native material tile " +
                                 std::to_string(tile.tile_number));
      const auto release = qScopeGuard([&] { BKE_image_release_ibuf(&image, buffer, nullptr); });

      const bool explicit_decode = image.source == IMA_SRC_FILE && image.type == IMA_TYPE_IMAGE;
      results.append(QJsonObject{
          {"image", QString::fromUtf8(image.id.name + 2)},
          {"tile", tile.tile_number},
          {"width", buffer->x},
          {"height", buffer->y},
          {"float", buffer->float_data() != nullptr},
          {"decode_ms", timer.nsecsElapsed() / 1e6},
          {"shared_decoded_source", reused},
          {"explicit_graph_space_decode", explicit_decode},
          {"normalization_policy_version",
           explicit_decode ? native_material_decode_policy_version : 0},
          {"graph_recorded_space", QString::fromUtf8(graph.main->colorspace.scene_linear_name)},
          {"explicit_float_space",
           buffer->float_buffer.colorspace ?
               QString::fromUtf8(
                   IMB_colormanagement_colorspace_get_name(buffer->float_buffer.colorspace)) :
               QString{}},
          {"source_space_override", QString::fromUtf8(image.colorspace_settings.name)}});
    }
  }
  if (cancel.load())
    throw std::runtime_error("Native material image loading cancelled");
  graph.tiled_image_resources = std::move(tiled_candidates);
  return results;
}
namespace {
[[noreturn]] void image_reject(const std::string &reason)
{
  throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                   "Native fragment image binding: " + reason);
}
}  // namespace
NativeFragmentImageContext NativeFragmentImageContext::capture(
    const PreparedNativeFragmentNormal &prepared, bool require_decoded)
{
  using namespace blender;
  NativeFragmentImageContext result;
  if (!prepared.source || !prepared.source->main || !prepared.tree ||
      BLI_findindex(&prepared.source->main->nodetrees, &prepared.tree->id) < 0)
    image_reject("source/tree owner is missing");
  result.source_ = prepared.source;
  std::set<const bNode *> visited;
  std::vector<const bNode *> pending;
  QJsonArray keys;
  for (const auto &node : prepared.tree->nodes)
    if (STREQ(node.idname, "ShaderNodeOutputAOV"))
      pending.push_back(&node);
  while (!pending.empty()) {
    const auto *node = pending.back();
    pending.pop_back();
    if (!visited.insert(node).second)
      continue;
    for (const auto &edge : prepared.tree->links)
      if (edge.tonode == node)
        pending.push_back(edge.fromnode);
    if (STREQ(node->idname, "ShaderNodeAttribute") && node->storage &&
        STREQ(static_cast<const NodeShaderAttribute *>(node->storage)->name, "BTS_Coord_UV"))
      result.default_uv_ = true;
    if (STREQ(node->idname, "ShaderNodeUVMap") && node->storage &&
        static_cast<const NodeShaderUVMap *>(node->storage)->uv_map[0] == 0)
      result.default_uv_ = true;
    if (STREQ(node->idname, "ShaderNodeTexEnvironment"))
      image_reject("Environment requires a separate retained owner/coordinate contract");
    if (!STREQ(node->idname, "ShaderNodeTexImage"))
      continue;
    if (!node->id || GS(node->id->name) != ID_IM)
      image_reject("reachable Image Texture has no actual Image owner");
    auto *image = reinterpret_cast<Image *>(node->id);
    if (BLI_findindex(&prepared.source->main->images, &image->id) < 0)
      image_reject("Image ID is outside its source Main");
    const auto *settings = static_cast<const NodeTexImage *>(node->storage);
    if (!settings)
      image_reject("Image Texture settings are missing");
    const bool tiled = image->source == IMA_SRC_TILED;
    if ((image->source != IMA_SRC_FILE && !tiled) || image->type != IMA_TYPE_IMAGE ||
        (!tiled && BLI_listbase_count(&image->tiles) != 1) || image->flag & IMA_USE_VIEWS)
      image_reject(
          "retained Image requires an ordinary FILE or TILED source; generated/movie/sequence/multiview inputs need a separate owner");
    if (tiled)
      try {
        (void)native_material_tiled_image_pattern(*prepared.source->main, *image);
      }
      catch (const std::exception &error) {
        image_reject(error.what());
      }
    const auto &user = settings->iuser;
    if (user.scene || user.pass || user.layer || user.multi_index || user.view ||
        user.multiview_eye)
      image_reject("nondefault ImageUser pass/layer/view is unsupported");
    if (tiled && !native_tiled_default_user_tile(user.tile))
      image_reject("nondefault TILED ImageUser tile selection requires a separate 2D owner");
    if (settings->projection != SHD_PROJ_FLAT && settings->projection != SHD_PROJ_BOX &&
        settings->projection != SHD_PROJ_SPHERE && settings->projection != SHD_PROJ_TUBE)
      image_reject("unknown Image Texture projection");
    result.object_matrices_ |= settings->projection == SHD_PROJ_BOX;
    if (settings->interpolation != SHD_INTERP_LINEAR &&
        settings->interpolation != SHD_INTERP_CLOSEST &&
        settings->interpolation != SHD_INTERP_CUBIC && settings->interpolation != SHD_INTERP_SMART)
      image_reject("unknown Image Texture interpolation");
    if (settings->extension != SHD_IMAGE_EXTENSION_REPEAT &&
        settings->extension != SHD_IMAGE_EXTENSION_EXTEND &&
        settings->extension != SHD_IMAGE_EXTENSION_CLIP &&
        settings->extension != SHD_IMAGE_EXTENSION_MIRROR)
      image_reject("unknown Image Texture extension");
    if (!std::isfinite(settings->projection_blend))
      image_reject("nonfinite Image Texture sampling metadata");
    const auto *vector = bke::node_find_enabled_input_socket(*const_cast<bNode *>(node), "Vector");
    if (!vector)
      image_reject("Image Texture Vector input is missing");
    result.default_uv_ |= !vector->link;
    const auto found = std::find_if(prepared.source->image_resources.begin(),
                                    prepared.source->image_resources.end(),
                                    [&](const auto &entry) { return entry.first == image; });
    std::shared_ptr<NativeMaterialImageResource> decoded =
        found == prepared.source->image_resources.end() ? nullptr : found->second;
    std::shared_ptr<const NativeMaterialImageTileSet> tile_set;
    if (tiled) {
      const auto owned = std::find_if(prepared.source->tiled_image_resources.begin(),
                                      prepared.source->tiled_image_resources.end(),
                                      [&](const auto &entry) { return entry.first == image; });
      if (owned != prepared.source->tiled_image_resources.end())
        tile_set = owned->second;
      if (require_decoded &&
          (!tile_set || tile_set->key().isEmpty() ||
           tile_set->tiles().size() != native_material_tiled_image_numbers(*image).size()))
        image_reject("required TILED source has no complete retained worker inventory");
    }
    else if (require_decoded &&
             (!decoded || decoded->key().isEmpty() || !decoded->buffer() ||
              decoded->buffer()->x <= 0 || decoded->buffer()->y <= 0 ||
              (!decoded->buffer()->float_data() && !decoded->buffer()->byte_data())))
      image_reject("required single-file source has no retained worker-decoded pixels");
    auto binding = std::find_if(result.bindings_.begin(),
                                result.bindings_.end(),
                                [&](const auto &binding) { return binding.image == image; });
    if (binding == result.bindings_.end()) {
      result.bindings_.push_back({image, decoded, tile_set});
      binding = std::prev(result.bindings_.end());
    }
    binding->tiled_mapping |= tiled && settings->projection == SHD_PROJ_FLAT;
    binding->first_tile_2d |= tiled && settings->projection != SHD_PROJ_FLAT;
    keys.append(QJsonArray{tiled ? (tile_set ? tile_set->key() : QString{}) :
                                   (decoded ? decoded->key() : QString{}),
                           tiled && settings->projection != SHD_PROJ_FLAT ?
                               static_cast<const ImageTile *>(image->tiles.first)->tile_number :
                               0,
                           int(settings->projection),
                           int(settings->interpolation),
                           int(settings->extension),
                           settings->projection_blend,
                           QString::fromUtf8(image->colorspace_settings.name),
                           int(image->alpha_mode),
                           int(image->flag & IMA_HIGH_BITDEPTH),
                           int(user.pass),
                           int(user.layer),
                           int(user.multi_index),
                           int(user.view)});
  }
  const auto bytes = QJsonDocument(keys).toJson(QJsonDocument::Compact);
  result.signature_.assign(bytes.constData(), bytes.size());
  return result;
}
void NativeFragmentImageContext::validate_storage(blender::Image *image,
                                                  int width,
                                                  int height,
                                                  bool single_2d,
                                                  NativeFragmentImagePrecision precision) const
{
  const auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const auto &binding) {
    return binding.image == image;
  });
  if (found == bindings_.end() || !found->decoded || !found->decoded->buffer())
    image_reject("sampled Image has no retained decoded binding");
  const auto *buffer = found->decoded->buffer();
  const auto format = blender::IMB_gpu_get_texture_format(
      buffer, bool(image->flag & blender::IMA_HIGH_BITDEPTH), true);
  const auto expected = format == blender::gpu::TextureFormat::SFLOAT_32_32_32_32 ?
                            NativeFragmentImagePrecision::Float32Rgba :
                        format == blender::gpu::TextureFormat::SFLOAT_16_16_16_16 ?
                            NativeFragmentImagePrecision::Float16Rgba :
                            NativeFragmentImagePrecision::Unsupported;
  if (!single_2d || width != buffer->x || height != buffer->y || precision != expected ||
      expected == NativeFragmentImagePrecision::Unsupported)
    image_reject(
        "sampled GPU storage dimensions/precision differ from the retained decoded source");
}
void NativeFragmentImageContext::validate_uv(const NativeFragmentGeometry &geometry,
                                             std::string_view source_uv) const
{
  if (!default_uv_)
    return;
  if (geometry.objects().empty())
    image_reject("default image UV has no captured owner streams");
  std::vector<bool> matched(geometry.objects().size(), false);
  for (const auto &stream : geometry.streams()) {
    if (stream.object >= matched.size())
      image_reject("captured UV stream owner is invalid");
    bool selected = false, render = false;
    for (const auto &request : stream.requests) {
      render |= request.kind == NativeFragmentUvKind::DefaultRender;
      selected |= source_uv.empty() ?
                      request.kind == NativeFragmentUvKind::DefaultRender :
                      request.kind == NativeFragmentUvKind::Named && request.name == source_uv;
    }
    if (selected && render)
      matched[stream.object] = true;
  }
  if (std::find(matched.begin(), matched.end(), false) != matched.end())
    image_reject(
        "Image Texture default UV and NormalMap source UV do not resolve to the same captured stream");
}
namespace {
void prepare_world_images(const std::shared_ptr<NativeMaterialGraph> &graph,
                          const std::atomic_bool &cancel,
                          const std::span<const NativeMaterialImageReference> *references)
{
  if (!graph || !graph->world_normal_tree || !graph->world_normal_error.isEmpty())
    return;
  auto before = graph->tiled_image_resources;
  try {
    auto inventory = NativeFragmentImageContext::capture({graph, graph->world_normal_tree}, false);
    if (inventory.bindings().empty())
      return;
    std::array<blender::bNodeTree *, 1> roots{graph->world_normal_tree};
    prepare_native_material_images(*graph, cancel, roots, false);
    if (references)
      validate_native_material_tiled_sources(*graph, *references, cancel);
    NativeFragmentImageContext::capture({graph, graph->world_normal_tree});
  }
  catch (const std::exception &error) {
    graph->tiled_image_resources = std::move(before);
    if (cancel.load())
      throw;
    graph->world_normal_error = QString::fromUtf8(error.what());
  }
}
}  // namespace
void prepare_native_world_normal_images(const std::shared_ptr<NativeMaterialGraph> &graph,
                                        const std::atomic_bool &cancel)
{
  prepare_world_images(graph, cancel, nullptr);
}
void prepare_native_world_normal_images(const std::shared_ptr<NativeMaterialGraph> &graph,
                                        const std::atomic_bool &cancel,
                                        std::span<const NativeMaterialImageReference> references)
{
  prepare_world_images(graph, cancel, &references);
}

void validate_native_material_tiled_sources(
    const NativeMaterialGraph &graph,
    std::span<const NativeMaterialImageReference> references,
    const std::atomic_bool &cancel)
{
  for (const auto &[image, set] : graph.tiled_image_resources) {
    if (cancel.load())
      throw std::runtime_error("Native tiled archive validation cancelled");
    if (!image || !set)
      throw std::invalid_argument("Native tiled archive has no complete prepared inventory");
    const auto name = QString::fromUtf8(image->id.name + 2);
    const auto found = std::find_if(
        references.begin(), references.end(), [&](const auto &reference) {
          return reference.image == name;
        });
    if (found != references.end()) {
      found->validate();
      if (found->source != NativeMaterialImageSourceKind::Tiled ||
          set->tiles().size() != found->tiles.size())
        throw std::invalid_argument(
            "Native tiled prepared source differs from its encoded archive manifest");
      for (const auto &tile : found->tiles) {
        const auto owner = set->tiles().find(tile.tile);
        if (owner == set->tiles().end() || !owner->second ||
            owner->second->encoded_sha256() != tile.sha256)
          throw std::runtime_error((name + " tile " + QString::number(tile.tile) +
                                    ": decoded source differs from immutable archive bytes")
                                       .toStdString());
      }
    }
    else {
      if (image->packedfiles.is_empty())
        throw std::invalid_argument(
            "Native tiled source lacks an immutable encoded archive closure");
      for (const auto &[number, owner] : set->tiles())
        if (!owner || owner->encoded_sha256() != native_material_encoded_image_sha256(
                                                     *graph.main, *image, number, cancel))
          throw std::runtime_error((name + " tile " + QString::number(number) +
                                    ": decoded source differs from packed archive bytes")
                                       .toStdString());
    }
  }
  if (cancel.load())
    throw std::runtime_error("Native tiled archive validation cancelled");
}

}  // namespace bts
