// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialStackTest.hh"
#include "NativeMaterialGraph.hh"
#include "GpuLayerPreview.hh"
#include "FilterPanel.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QScopeGuard>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
namespace bts {
QJsonObject verify_native_material_stack()
{
  using namespace blender;
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
  auto *material = BKE_material_add(main.get(), "Native stack fixture");
  auto &tree = *material->nodetree;
  auto *pbr = bke::node_add_node(nullptr, tree, UString("ShaderNodeBsdfPrincipled"));
  auto *out = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,
                     *pbr,
                     *static_cast<bNodeSocket *>(pbr->outputs.first),
                     *out,
                     *bke::node_find_enabled_input_socket(*out, "Surface"));
  BKE_ntree_update_after_single_tree_change(*main, tree);
  NativeMaterialDescription description;
  description.material = "Native stack fixture";
  description.surface_type = "ShaderNodeBsdfPrincipled";
  NativeMaterialParameter color;
  color.identifier = "Base Color";
  color.name = "Color";
  color.type = NativeParameterType::Color;
  color.value = {.8, .2, .1, 1};
  color.maximum = 65504;
  description.parameters.push_back(color);
  NativeMaterialParameter rough;
  rough.identifier = "Roughness";
  rough.name = "Roughness";
  rough.value[0] = .2;
  description.parameters.push_back(rough);
  std::atomic_bool cancelled = false;
  auto graph = std::make_shared<NativeMaterialGraph>(
      std::move(main), material, description, cancelled);
  TextureSet set;
  set.texture_resolution = 512;
  set.tiles = {1001, 1002};
  set.precision[0] = ChannelPrecision::RGB32F;
  set.precision[2] = ChannelPrecision::L32F;
  TextureDocument::add_fill(set);
  const auto base = set.selected_layer;
  TextureDocument::layer(set, base).values.base_color = {.1, .2, .9, 1};
  TextureDocument::add_fill(set);
  const auto native = set.selected_layer;
  auto &layer = TextureDocument::layer(set, native);
  layer.fill.material_resource = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  layer.fill.native_material = std::make_shared<const NativeMaterialDescription>(description);
  layer.opacity[0] = .5;
  const auto collect = qScopeGuard([] { GpuLayerPreview::collect_retired(); });
  GpuLayerPreview preview;
  preview.install_native_material(layer.fill.material_resource, "native-stack-template", graph);
  using Texture = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  Texture target(GPU_texture_create_2d("Native stack target",
                                       512,
                                       512,
                                       1,
                                       gpu::TextureFormat::SFLOAT_32_32_32_32,
                                       GPU_TEXTURE_USAGE_GENERAL,
                                       nullptr),
                 GPU_texture_free);
  require(bool(target), "Native stack target missing");
  int captures = 0;
  float maximum = 0;
  const auto render = [&](int channel, int tile) {
    for (int attempt = 0; attempt < 64; ++attempt) {
      preview.begin_frame();
      const auto result = preview.update(set, channel, target.get(), tile);
      if (result == GpuPreviewUpdate::Updated)
        return;
      require(result != GpuPreviewUpdate::Unsupported, "Native stack was rejected");
    }
    throw std::runtime_error("Native stack did not settle");
  };
  const auto check = [&](int channel, int tile, std::array<float, 3> expected) {
    render(channel, tile);
    auto *pixels = static_cast<float *>(GPU_texture_read(target.get(), GPU_DATA_FLOAT, 0));
    require(pixels, "Native stack oracle read failed");
    const auto free = qScopeGuard([&] { MEM_delete(pixels); });
    for (int i = 0; i < 512 * 512; ++i) {
      for (int c = 0; c < 3; ++c)
        maximum = std::max(maximum, std::abs(pixels[i * 4 + c] - expected[c]));
      require(pixels[i * 4 + 3] == 1, "Native stack lost final coverage");
    }
    require(maximum < 2e-6, "Native source/mask/group blend differs from oracle");
    ++captures;
  };
  for (int tile : set.tiles)
    check(0, tile, {.45, .2, .5});
  const auto warm = preview.native_statistics();
  for (int tile : set.tiles)
    check(0, tile, {.45, .2, .5});
  require(preview.native_statistics()[2] == warm[2], "Unchanged native source was recomputed");
  auto modified = description;
  modified.parameters[1].value[0] = .8;
  TextureDocument::layer(set, native).fill.native_material =
      std::make_shared<const NativeMaterialDescription>(modified);
  for (int tile : set.tiles)
    check(0, tile, {.45, .2, .5});
  require(preview.native_statistics()[2] == warm[2], "Roughness input invalidated native Color");
  for (int tile : set.tiles)
    check(2, tile, {.8, .8, .8});
  // Existing mask, group and blend operations consume the native source normally.
  auto &masked = TextureDocument::layer(set, native);
  masked.mask = LayerMask{};
  masked.mask->background = .4;
  for (int tile : set.tiles)
    check(0, tile, {.24, .2, .74});
  TextureLayer folder;
  folder.kind = LayerKind::Folder;
  folder.depth = 0;
  folder.name = "Native folder";
  folder.opacity[0] = .5;
  TextureDocument::layer(set, native).depth = 1;
  set.layers.push_back(folder);
  for (int tile : set.tiles)
    check(0, tile, {.17, .2, .82});
  const auto group = preview.native_statistics();
  set.layers.back().opacity[0] = .25;
  for (int tile : set.tiles)
    check(0, tile, {.135, .2, .86});
  require(preview.native_statistics()[2] == group[2],
          "Folder opacity invalidated a cached native child");
  // A new immutable instance restores the prior value without replacing source IDs.
  auto &restored = TextureDocument::layer(set, native);
  restored.fill.native_material = std::make_shared<const NativeMaterialDescription>(description);
  restored.mask.reset();
  restored.depth = 0;
  set.layers.pop_back();
  for (int tile : set.tiles)
    check(0, tile, {.45, .2, .5});
  require(preview.native_statistics()[0] == 1 && preview.native_statistics()[1] == 2,
          "Native stack duplicated source/program resources");
  // Artist filter controls must affect the native material output, including
  // repeated edits of the same GPU parameter rows and a mask's scalar Value.
  {
    auto &material_layer = TextureDocument::layer(set, native);
    material_layer.opacity[0] = 1;
    TextureDocument::add_filter_effect(set, 0);
    auto &effect = material_layer.effects.back();
    FilterPanel panel(nullptr);
    panel.synchronize(&set, &effect.filter, 0);
    panel.changed =
        [&](const FilterSettings &filter, int channel, float opacity, LayerBlend blend) {
          effect.filter = filter;
          effect.channel = channel;
          effect.opacity = opacity;
          effect.blending = blend;
        };
    panel.findChild<QAction *>("filterHsv")->trigger();
    for (double hue : {.25, .5, -.25, 0.}) {
      panel.findChild<QDoubleSpinBox *>("filterHue")->setValue(hue);
      for (int tile : set.tiles)
        check(0, tile, filter_rgb(effect.filter, {.8, .2, .1}));
    }
    for (int resource = 1; resource <= 6; ++resource) {
      effect.filter = {};
      effect.filter.resource = resource;
      effect.filter.hue = .5;
      effect.filter.contrast = 1.5;
      effect.filter.output_high = .7;
      for (int tile : set.tiles)
        check(0, tile, filter_rgb(effect.filter, {.8, .2, .1}));
    }
    effect.blending = LayerBlend::Disabled;
    for (int tile : set.tiles)
      check(0, tile, {.8, .2, .1});
    material_layer.effects.clear();
    material_layer.mask = LayerMask{};
    material_layer.mask->background = .8;
    MaskEffect mask;
    mask.kind = MaskEffectKind::Filter;
    mask.filter.resource = 4;
    mask.filter.value_scale = .5;
    material_layer.mask->effects.push_back(mask);
    for (int tile : set.tiles)
      check(0, tile, {.38, .2, .58});
    material_layer.mask.reset();
  }
  // A UV-driven native graph uses the ordinary fill affine transform on each tile.
  {
    std::unique_ptr<Main, decltype(&BKE_main_free)> owner(BKE_main_new(), BKE_main_free);
    auto *mat = BKE_material_add(owner.get(), "Native UV stack fixture");
    auto &nodes = *mat->nodetree;
    auto *shader = bke::node_add_node(nullptr, nodes, UString("ShaderNodeBsdfPrincipled"));
    auto *coords = bke::node_add_node(nullptr, nodes, UString("ShaderNodeTexCoord"));
    auto *output = bke::node_add_node(nullptr, nodes, UString("ShaderNodeOutputMaterial"));
    bNodeSocket *uv = nullptr;
    for (auto &candidate : coords->outputs)
      if (std::string_view(candidate.identifier) == "UV")
        uv = &candidate;
    require(uv, "Native UV socket missing");
    bke::node_add_link(
        nodes, *coords, *uv, *shader, *bke::node_find_enabled_input_socket(*shader, "Base Color"));
    bke::node_add_link(nodes,
                       *shader,
                       *static_cast<bNodeSocket *>(shader->outputs.first),
                       *output,
                       *bke::node_find_enabled_input_socket(*output, "Surface"));
    BKE_ntree_update_after_single_tree_change(*owner, nodes);
    NativeMaterialDescription source;
    source.material = "Native UV fixture";
    source.surface_type = "ShaderNodeBsdfPrincipled";
    auto uv_graph = std::make_shared<NativeMaterialGraph>(
        std::move(owner), mat, source, cancelled);
    auto &fill = TextureDocument::layer(set, native).fill;
    fill.material_resource = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    fill.native_material = std::make_shared<const NativeMaterialDescription>(source);
    TextureDocument::layer(set, native).opacity[0] = 1;
    preview.install_native_material(fill.material_resource, "native-uv-template", uv_graph);
    fill.linked =
        false;  // Intentionally anisotropic authored UV fixture; keep model metadata valid.
    for (bool transformed : {false, true}) {
      fill.scale = transformed ? std::array<double, 2>{2, .7} : std::array<double, 2>{1, 1};
      fill.offset = transformed ? std::array<double, 2>{.17, -.23} : std::array<double, 2>{0, 0};
      fill.rotation = transformed ? 37 : 0;
      for (int tile : set.tiles) {
        render(0, tile);
        auto *actual = static_cast<float *>(GPU_texture_read(target.get(), GPU_DATA_FLOAT, 0));
        require(actual, "Native UV stack oracle read failed");
        const auto free = qScopeGuard([&] { MEM_delete(actual); });
        for (int y = 0; y < 512; ++y)
          for (int x = 0; x < 512; ++x) {
            const auto expected = fill_uv_map(
                fill, (x + .5) / 512 + (tile - 1001) % 10, (y + .5) / 512 + (tile - 1001) / 10);
            maximum = std::max({maximum,
                                float(std::abs(actual[(y * 512 + x) * 4] - expected[0])),
                                float(std::abs(actual[(y * 512 + x) * 4 + 1] - expected[1]))});
          }
        ++captures;
      }
      require(maximum < 2e-6,
              "Native UV transforms or UDIM tile offset differ from ordinary fills");
    }
  }
  return {{"tiles", QJsonArray{1001, 1002}},
          {"resolution", 512},
          {"captures", captures},
          {"maximum_error", maximum},
          {"unchanged_source_cached", true},
          {"per_channel_invalidation", true},
          {"mask", true},
          {"folder_cache", true},
          {"immutable_instance_restore", true},
          {"uv_transform_udim", true},
          {"content_filters", true},
          {"hue_parameter_refresh", true},
          {"mask_hsv_value", true},
          {"sources", qint64(preview.native_statistics()[0])},
          {"programs", qint64(preview.native_statistics()[1])},
          {"region_dispatches", qint64(preview.native_statistics()[2])},
          {"cpu_composition", false}};
}
}  // namespace bts
