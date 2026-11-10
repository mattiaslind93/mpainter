// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Private source-binding diagnostic only. A distinct type avoids changing the
// already-shipped GpuTap definition or recompiling its unrelated consumers.
#include "MaterialLayeringGpuTap.hh"
#include "BKE_image_hosted.hh"
#include <map>
#include <optional>
#include <utility>
namespace bts::pd149_sources_test {
using namespace blender;
using pd149_test::require;
enum class Output { Color, Metallic, Roughness, AO, Height, InternalNormal };
// Explicit caller audit of the saved stock graph, including every reached
// carrier/mask Image. This is not permission to lazy-bind a runtime alias.
struct StockSavedImages {
  std::set<Image *> audited;
};
class SourceGpuTap final {
  struct TreeDelete {
    void operator()(bNodeTree *tree) const
    {
      if (tree)
        BKE_id_free_ex(nullptr,
                       &tree->id,
                       LIB_ID_FREE_NO_MAIN | LIB_ID_FREE_NO_USER_REFCOUNT | LIB_ID_FREE_NO_DEG_TAG,
                       false);
    }
  };
  Material &source_;
  QJsonObject signature_;
  std::thread::id owner_ = std::this_thread::get_id();
  GPUContext *context_ = GPU_context_active_get();
  std::unique_ptr<bNodeTree, TreeDelete> tree_;
  std::unique_ptr<NativeMaterialGpuProgram> program_;
  std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)> target_{nullptr, GPU_texture_free};
  std::vector<std::pair<Image *, gpu::Texture *>> images_;
  int width_, height_;
  std::optional<StockSavedImages> stock_;
  std::map<Image *, QJsonObject> stock_descriptors_;
  static bool immutable(const Image &image)
  {
    return image.runtime && (image.runtime->gpuflag & IMA_HOSTED_IMMUTABLE_SOURCE);
  }
  std::string image_error(const Image &image, const char *reason) const
  {
    return std::string(reason) + " image=" + image.id.name +
           " uid=" + std::to_string(image.id.session_uid) +
           " immutable=" + (immutable(image) ? "1" : "0") + " whitelisted=" +
           (stock_ && stock_->audited.contains(const_cast<Image *>(&image)) ? "1" : "0");
  }
  QJsonObject stock_descriptor(Image &image) const
  {
    const auto fail = [&](bool okay, const char *reason) {
      if (!okay)
        throw std::runtime_error(image_error(image, reason));
    };
    const auto cached = BKE_image_gpu_override_texture(&image);
    auto *texture = cached.texture ? *cached.texture : nullptr;
    const bool tiled = image.source == IMA_SRC_TILED;
    fail(texture && GPU_texture_dimensions(texture) == 2 &&
             GPU_texture_is_array(texture) == tiled && GPU_texture_width(texture) > 0 &&
             GPU_texture_height(texture) > 0 && !GPU_texture_has_integer_format(texture) &&
             !GPU_texture_has_depth_format(texture) && !GPU_texture_has_stencil_format(texture),
         "Saved stock sampler has no valid cached GPU allocation");
    fail(!tiled || (cached.tile_mapping && *cached.tile_mapping),
         "Saved stock sampler has no cached UDIM mapping");
    QJsonArray regions;
    for (const auto &tile : image.tiles) {
      ImageGpuTileRegion region;
      fail(BKE_image_gpu_override_tile_region(&image, tile.tile_number, region),
           "Saved stock sampler tile is not resident");
      fail(region.width > 0 && region.height > 0 && region.x >= 0 && region.y >= 0 &&
               region.x <= GPU_texture_width(texture) - region.width &&
               region.y <= GPU_texture_height(texture) - region.height && region.layer >= 0 &&
               region.layer < GPU_texture_layer_count(texture),
           "Saved stock sampler tile lies outside its allocation");
      regions.append(QJsonObject{{"tile", tile.tile_number},
                                 {"x", region.x},
                                 {"y", region.y},
                                 {"width", region.width},
                                 {"height", region.height},
                                 {"layer", region.layer}});
    }
    fail(!regions.isEmpty(), "Saved stock sampler has no declared tiles");
    return QJsonObject{
        {"texture", pd149_test::pointer(texture)},
        {"mapping", pd149_test::pointer(cached.tile_mapping ? *cached.tile_mapping : nullptr)},
        {"source", image.source},
        {"alpha", image.alpha_mode},
        {"space", image.colorspace_settings.name},
        {"flags", image.flag},
        {"width", GPU_texture_width(texture)},
        {"height", GPU_texture_height(texture)},
        {"layers", GPU_texture_layer_count(texture)},
        {"format", GPU_texture_format_name(GPU_texture_format(texture))},
        {"regions", regions},
        {"immutable", immutable(image)},
        {"native_generated", native_gpu_image(&image)}};
  }
  void check() const
  {
    require(context_ && owner_ == std::this_thread::get_id() &&
                GPU_context_active_get() == context_,
            "Source tap escaped its GPU owner");
    require(pd149_test::live_signature(source_) == signature_,
            "Source tap mutated the real apply graph");
    for (const auto &[image, texture] : images_) {
      const auto cached = BKE_image_gpu_override_texture(image);
      if (!cached.texture || *cached.texture != texture)
        throw std::runtime_error(
            image_error(*image, "Source tap changed a live Image allocation"));
    }
    for (const auto &[image, descriptor] : stock_descriptors_)
      if (stock_descriptor(*image) != descriptor)
        throw std::runtime_error(
            image_error(*image, "Source tap changed a saved stock Image descriptor/allocation"));
  }

 public:
  SourceGpuTap(Material &source, Output which, int width, int height)
      : SourceGpuTap(source, which, width, height, std::nullopt)
  {
  }
  SourceGpuTap(
      Material &source, Output which, int width, int height, const StockSavedImages &stock)
      : SourceGpuTap(source, which, width, height, std::optional<StockSavedImages>(stock))
  {
  }

 private:
  SourceGpuTap(
      Material &source, Output which, int width, int height, std::optional<StockSavedImages> stock)
      : source_(source),
        signature_(pd149_test::live_signature(source)),
        width_(width),
        height_(height),
        stock_(std::move(stock))
  {
    require(context_ && width > 0 && height > 0,
            "Source tap requires an owner context and positive grid");
    const auto selected = [&] {
      if (which == Output::InternalNormal) {
        auto *normal = bke::node_find_node_by_name(*source.nodetree, "__BTS_LayeredNormal");
        require(normal && STREQ(normal->idname, "ShaderNodeNormalMap"),
                "Actual layered NormalMap missing");
        const auto &link = pd149_test::incoming(*source.nodetree,
                                                pd149_test::input(*normal, "Color"));
        return pd149_test::Endpoint{link.fromnode, link.fromsock->identifier, false};
      }
      if (which != Output::Height)
        return pd149_test::endpoint(source,
                                    which == Output::Color     ? pd149_test::Output::Color :
                                    which == Output::Metallic  ? pd149_test::Output::Metallic :
                                    which == Output::Roughness ? pd149_test::Output::Roughness :
                                                                 pd149_test::Output::AO);
      auto *bump = bke::node_find_node_by_name(*source.nodetree, "__BTS_LayeredBump");
      require(bump && STREQ(bump->idname, "ShaderNodeBump"), "Actual layered Bump node missing");
      const auto &link = pd149_test::incoming(*source.nodetree,
                                              pd149_test::input(*bump, "Height"));
      return pd149_test::Endpoint{link.fromnode, link.fromsock->identifier, true};
    }();
    std::vector<bNode *> pending{selected.node};
    std::set<bNode *> visited;
    std::set<Image *> seen;
    while (!pending.empty()) {
      auto *node = pending.back();
      pending.pop_back();
      if (!visited.insert(node).second)
        continue;
      if (stock_) {
        require(!node->id || GS(node->id->name) != ID_NT,
                "Saved stock tap excludes upstream node groups");
        require(!node->id || GS(node->id->name) != ID_IM ||
                    STREQ(node->idname, "ShaderNodeTexImage"),
                "Saved stock tap excludes other Image sampler families");
      }
      if (STREQ(node->idname, "ShaderNodeTexImage")) {
        require(node->id && GS(node->id->name) == ID_IM, "Actual source sampler Image missing");
        seen.insert(reinterpret_cast<Image *>(node->id));
      }
      for (const auto &link : source.nodetree->links)
        if (link.tonode == node) {
          require(link.fromnode && (link.flag & NODE_LINK_VALID) && !(link.flag & NODE_LINK_MUTED),
                  "Inactive source tap dependency");
          pending.push_back(link.fromnode);
        }
    }
    // Finish membership/source admission before the first stock upload.
    if (stock_)
      for (auto *image : seen) {
        if (!stock_->audited.contains(image))
          throw std::runtime_error(
              image_error(*image, "Saved stock sampler was not explicitly audited"));
        if (image->source != IMA_SRC_FILE && image->source != IMA_SRC_TILED &&
            image->source != IMA_SRC_GENERATED)
          throw std::runtime_error(
              image_error(*image, "Saved stock sampler has an unsupported source"));
      }
    for (auto *image : seen) {
      // Hosted runtime sources, including immutable aliases, always retain the
      // existing exact cached-allocation requirement, even when whitelisted.
      const bool hosted = image->runtime && (image->runtime->gpuflag &
                                             (IMA_HOSTED_GPU_OVERRIDE | IMA_HOSTED_CPU_MIRROR |
                                              IMA_HOSTED_IMMUTABLE_SOURCE));
      if (stock_ && !hosted) {
        // Only the caller-audited saved Image uses ordinary stock loading/upload.
        // No source-adapter, canonical alias, image rewiring or CPU conversion.
        const auto stock_texture = BKE_image_get_gpu_material_texture(image, nullptr, true);
        if (!stock_texture.texture || !*stock_texture.texture || !BKE_image_has_loaded_ibuf(image))
          throw std::runtime_error(
              image_error(*image, "Saved stock sampler did not load decoded GPU storage"));
        stock_descriptors_.emplace(image, stock_descriptor(*image));
      }
      const auto cached = BKE_image_gpu_override_texture(image);
      if (!cached.texture || !*cached.texture)
        throw std::runtime_error(image_error(*image, "Source tap input not resident"));
      images_.emplace_back(image, *cached.texture);
    }
    tree_.reset(reinterpret_cast<bNodeTree *>(
        BKE_id_copy_in_lib(nullptr,
                           std::optional<Library *>{nullptr},
                           &source.nodetree->id,
                           std::optional<const ID *>{nullptr},
                           nullptr,
                           LIB_ID_COPY_LOCALIZE | LIB_ID_COPY_NO_ANIMDATA)));
    require(tree_ && !BKE_id_owner_get(&tree_->id, false),
            "Source tap copy must remain outside Main");
    tree_->flag |= NTREE_IS_GPU_SHADER_INTERNAL;
    bNode *from = nullptr;
    std::vector<bNode *> remove;
    for (auto &node : tree_->nodes) {
      if (node.identifier == selected.node->identifier)
        from = &node;
      if (STREQ(node.idname, "ShaderNodeOutputMaterial") ||
          STREQ(node.idname, "ShaderNodeOutputAOV"))
        remove.push_back(&node);
    }
    require(from, "Copied source endpoint missing");
    for (auto *node : remove)
      bke::node_remove_node(nullptr, *tree_, *node, false, false);
    // The compute compiler consumes native context attributes, whereas EEVEE
    // supplies ObjectMatrices and camera helpers to the original TexCoord node.
    // Adapt ONLY this outside-Main diagnostic copy, as NativeMaterialGraph's
    // bind_context does. Mapping, source Images and all composition stay intact.
    std::vector<bNode *> coordinates;
    for (auto &node : tree_->nodes)
      if (STREQ(node.idname, "ShaderNodeTexCoord"))
        coordinates.push_back(&node);
    for (auto *coordinate : coordinates) {
      require(!(coordinate->flag & NODE_MUTED) && !(coordinate->custom1 & 1) && !coordinate->id,
              "Source tap cannot adapt instancer/reference-object coordinates");
      std::vector<bNodeLink *> uses;
      for (auto &link : tree_->links)
        if (link.fromnode == coordinate) {
          require(link.fromsock && link.tosock && link.tonode && (link.flag & NODE_LINK_VALID) &&
                      !(link.flag & NODE_LINK_MUTED) && STREQ(link.fromsock->identifier, "UV") &&
                      link.fromsock->type == SOCK_VECTOR && link.fromsock->is_available() &&
                      link.tosock->is_available(),
                  "Source tap only adapts active UV coordinate uses");
          uses.push_back(&link);
        }
      if (!uses.empty()) {
        auto *attribute = bke::node_add_node(nullptr, *tree_, UString("ShaderNodeAttribute"));
        require(attribute && attribute->storage, "Source tap native UV attribute missing");
        STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name, "BTS_Coord_UV");
        for (auto *link : uses) {
          bke::node_add_link(*tree_,
                             *attribute,
                             pd149_test::output(*attribute, "Vector"),
                             *link->tonode,
                             *link->tosock);
          bke::node_remove_link(tree_.get(), *link);
        }
      }
      bke::node_remove_node(nullptr, *tree_, *coordinate, false, false);
    }
    auto *aov = bke::node_add_node(nullptr, *tree_, UString("ShaderNodeOutputAOV"));
    require(aov, "Source AOV allocation failed");
    STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name, "BTS_Channel");
    // Scalar layer MixRGB results are grayscale. Select R explicitly in the copy
    // instead of involving Blender's global RGB-to-luminance conversion.
    auto *socket = &pd149_test::output(*from, selected.socket.c_str());
    if (selected.scalar && socket->type != SOCK_FLOAT) {
      auto *split = bke::node_add_node(nullptr, *tree_, UString("ShaderNodeSeparateXYZ"));
      require(split, "Source scalar AOV split missing");
      bke::node_add_link(*tree_, *from, *socket, *split, pd149_test::input(*split, "Vector"));
      from = split;
      socket = &pd149_test::output(*split, "X");
    }
    bke::node_add_link(*tree_,
                       *from,
                       *socket,
                       *aov,
                       pd149_test::input(*aov, selected.scalar ? "Value" : "Color"));
    BKE_ntree_update_without_main(*tree_);

    program_ = std::make_unique<NativeMaterialGpuProgram>(*tree_, 0, selected.scalar ? 1 : 0);
    target_.reset(GPU_texture_create_2d("Source diagnostic output",
                                        width,
                                        height,
                                        1,
                                        gpu::TextureFormat::SFLOAT_32_32_32_32,
                                        GPU_TEXTURE_USAGE_GENERAL,
                                        nullptr));
    require(bool(target_), "Source diagnostic target missing");
    check();
  }

 public:
  ~SourceGpuTap()
  {
    if (owner_ != std::this_thread::get_id() || GPU_context_active_get() != context_)
      qFatal("Source diagnostic GPU objects survived owner cleanup");
    target_.reset();
    program_.reset();
    tree_.reset();
  }
  std::vector<std::array<float, 4>> sample(int tile,
                                           const std::array<float, 9> &scene_to_ap1 = {
                                               1, 0, 0, 0, 1, 0, 0, 0, 1})
  {
    check();
    NativeMaterialGpuContext context;
    context.tile = tile;
    context.color_to_aces = scene_to_ap1;
    program_->dispatch(target_.get(), {}, context, {0, 0, width_, height_});
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
    auto *raw = static_cast<float *>(GPU_texture_read(target_.get(), GPU_DATA_FLOAT, 0));
    require(raw, "Source diagnostic readback missing");
    const auto release = qScopeGuard([&] { MEM_delete(raw); });
    std::vector<std::array<float, 4>> result(std::size_t(width_) * height_);
    std::memcpy(result.data(), raw, result.size() * sizeof(result[0]));
    check();
    return result;
  }
};
}  // namespace bts::pd149_sources_test
