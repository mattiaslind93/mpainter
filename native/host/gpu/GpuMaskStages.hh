// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuLayerEffects.hh"
#include "GpuSpatialParameters.hh"
#include "GpuMaskComparison.hh"
#include "GpuGeneratorSurface.hh"
namespace bts {
// Mask prefixes are shared by every material channel. Only metadata is examined
// here; rasterization, effect evaluation and blending happen on the GPU.
struct GpuMaskStage {
  enum Kind {
    Base = 1,
    Points = 2,
    Paint = 3,
    SpatialFill = 4,
    Anchor = 5,
    AnchorFill = 6,
    Compare = 7
  };
  Kind kind = Base;
  std::string id;
  const TilePaintPixels *pixels = nullptr;
  float background = 0, opacity = 1;
  LayerBlend blend = LayerBlend::Normal;
  std::vector<GpuEffectRow> rows;
  std::optional<GpuLayerParameters> spatial;
  std::shared_ptr<const FillSurfaceAtlas> geometry;
  std::shared_ptr<const FillTexture> bitmap;
  bool surface_required = true;
  std::string owner_key, anchor_key, anchor_id;
  std::shared_ptr<const std::vector<GpuMaskStage>> anchor_stages;
  QUuid comparison_layer;
  int comparison_channel = 0;
  AnchorReference reference;
  bool content_anchor = false;
  std::optional<MeshGeneratorSettings> generator;
  std::shared_ptr<const TextureLayer> group_owner;
};
inline bool gpu_mask_needs_stages(const TextureLayer &layer)
{
  return layer.mask && layer.mask->enabled &&
         std::any_of(layer.mask->effects.begin(), layer.mask->effects.end(), [](const auto &e) {
           return e.enabled &&
                  (e.kind == MaskEffectKind::Group || e.kind == MaskEffectKind::Anchor ||
                   (e.kind == MaskEffectKind::Generator &&
                    (generator_has_anchors(e) || e.triplanar_grunge || e.surface_fallback)) ||
                   (e.kind == MaskEffectKind::Filter && e.filter.resource >= 7) ||
                   (e.blending != LayerBlend::Disabled &&
                    (e.kind == MaskEffectKind::Paint ||
                     (e.kind == MaskEffectKind::Compare && (e.left < 2 || e.right < 2)) ||
                     (e.kind == MaskEffectKind::Fill &&
                      (!e.anchor_source.isNull() ||
                       fill_surface_projection(e.mapping.projection) ||
                       bool(e.mapping.projection.texture))))));
         });
}
class GpuMaskBuilder {
  const TextureSet &set;
  int tile;
  std::map<QUuid, std::shared_ptr<const std::vector<GpuMaskStage>>> cache;
  std::string key(const TextureLayer &layer) const
  {
    return set.id.toString().toStdString() + layer.id.toString().toStdString() + ":" +
           std::to_string(tile) + ":mask";
  }
  std::vector<GpuMaskStage> build_uncached(const TextureLayer &layer)
  {
    std::vector<GpuMaskStage> result;
    if (!layer.mask)
      return result;
    const auto &mask = *layer.mask;
    result.push_back({GpuMaskStage::Base, "base", &mask.pixels, mask.background});
    for (const auto &effect : mask.effects) {
      if (!effect.enabled)
        continue;
      if (effect.kind == MaskEffectKind::Group) {
        auto group = std::make_shared<TextureLayer>();
        group->id = effect.id;
        group->mask = LayerMask{};
        group->mask->background = 0;
        if (effect.group_effects)
          group->mask->effects = *effect.group_effects;
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::AnchorFill;
        stage.id = effect.id.toString().toStdString();
        stage.opacity = effect.opacity;
        stage.blend = effect.blending;
        stage.group_owner = group;
        stage.anchor_key = key(*group);
        auto children = build_uncached(*group);
        stage.anchor_id = children.back().id;
        stage.anchor_stages = std::make_shared<const std::vector<GpuMaskStage>>(
            std::move(children));
        result.push_back(std::move(stage));
      }
      else if (effect.kind == MaskEffectKind::Anchor) {
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::Anchor;
        stage.id = effect.id.toString().toStdString();
        result.push_back(std::move(stage));
      }
      else if (effect.kind == MaskEffectKind::Fill && !effect.anchor_source.isNull()) {
        if (effect.blending == LayerBlend::Disabled)
          continue;
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::AnchorFill;
        stage.id = effect.id.toString().toStdString();
        stage.opacity = effect.opacity;
        stage.blend = effect.blending;
        stage.anchor_id = effect.anchor_source.toString().toStdString();
        stage.comparison_layer = layer.id;
        stage.reference = effect.anchor_reference();
        if (const auto point = TextureDocument::anchor_point(set, effect.anchor_source); point)
          stage.content_anchor = !point->mask;
        if (!stage.content_anchor)
          for (const auto &producer : evaluation_layers(set)) {
            if (producer.id == layer.id)
              break;
            if (!producer.mask)
              continue;
            const bool found = std::any_of(
                producer.mask->effects.begin(), producer.mask->effects.end(), [&](const auto &e) {
                  return e.enabled && e.kind == MaskEffectKind::Anchor &&
                         e.id == effect.anchor_source;
                });
            if (found) {
              build(producer, true);
              const auto &all = *cache.at(producer.id);
              const auto point = std::find_if(all.begin(), all.end(), [&](const auto &part) {
                return part.id == stage.anchor_id;
              });
              stage.anchor_stages = std::make_shared<const std::vector<GpuMaskStage>>(
                  all.begin(), point == all.end() ? all.end() : point + 1);
              stage.anchor_key = key(producer);
              break;
            }
          }
        result.push_back(std::move(stage));
      }
      else if (effect.kind == MaskEffectKind::Paint) {
        if (effect.blending != LayerBlend::Disabled)
          result.push_back({GpuMaskStage::Paint,
                            effect.id.toString().toStdString(),
                            &effect.paint.pixels,
                            0,
                            effect.opacity,
                            effect.blending});
      }
      else if (effect.kind == MaskEffectKind::Fill && effect.anchor_source.isNull() &&
               (fill_surface_projection(effect.mapping.projection) ||
                effect.mapping.projection.texture))
      {
        if (effect.blending == LayerBlend::Disabled)
          continue;
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::SpatialFill;
        stage.id = effect.id.toString().toStdString();
        stage.background = effect.fill;
        stage.opacity = effect.blending == LayerBlend::Disabled ? 0 : effect.opacity;
        stage.blend = effect.blending;
        stage.surface_required = fill_surface_projection(effect.mapping.projection);
        stage.bitmap = effect.mapping.projection.texture;
        stage.geometry = stage.surface_required ?
                             (set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                                    set.projection_geometry) :
                             nullptr;
        stage.spatial = GpuLayerParameters{};
        auto &parameters = *stage.spatial;
        const auto &fill = effect.mapping.projection;
        if (fill.mode == 6 && stage.bitmap && stage.bitmap->tiles) {
          const auto member = stage.bitmap->tiles->find(tile);
          if (member == stage.bitmap->tiles->end()) {
            stage.bitmap.reset();
            stage.background = 0;
          }
          else
            stage.bitmap = member->second;
        }
        parameters.set("projection_mode", fill.mode);
        parameters.set("projection_wrap",
                       fill.mode == 6 ? 1 :
                       fill.mode == 0 ? (effect.mapping.wrap == 1 ? 4 :
                                         effect.mapping.wrap == 0 ? 0 :
                                                                    effect.mapping.wrap - 1) :
                                        fill.wrap);
        parameters.set("source_filter",
                       fill.mode == 1                ? 0 :
                       effect.mapping.filtering == 1 ? 2 :
                                                       fill.filtering);
        parameters.set("source_ignore_alpha", !fill.alpha_mask[0]);
        parameters.set("fill_resource_kind", stage.bitmap ? 1 : fill.atom ? 2 : 0);
        const float options[4] = {float(fill.border_width),
                                  float(fill.middle_dot),
                                  float(fill.border_dots),
                                  float(fill.invert)};
        parameters.set4("atom_options", options);
        auto mapping = fill;
        if (mapping.mode == 1) {
          mapping.offset = {};
          mapping.physical = false;
        }
        if (fill_spatial_projection(mapping))
          mapping.physical = false;
        mapping.native_per_uv = set.native_per_uv;
        mapping.cm_per_native = set.cm_per_native;
        const auto a = fill.mode == 6 ? std::array<double, 2>{0, 0} : fill_uv_map(mapping, 0, 0),
                   b = fill.mode == 6 ? std::array<double, 2>{1, 0} : fill_uv_map(mapping, 1, 0),
                   c = fill.mode == 6 ? std::array<double, 2>{0, 1} : fill_uv_map(mapping, 0, 1);
        const float u[3] = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])},
                    v[3] = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
        parameters.set3("map_u", u);
        parameters.set3("map_v", v);
        gpu_spatial_parameters(parameters, fill, stage.geometry.get(), set);
        result.push_back(std::move(stage));
      }
      else if (effect.kind == MaskEffectKind::Compare && (effect.left < 2 || effect.right < 2) &&
               effect.blending != LayerBlend::Disabled)
      {
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::Compare;
        stage.id = effect.id.toString().toStdString();
        stage.comparison_layer = layer.id;
        stage.comparison_channel = effect.compare_channel;
        TextureLayer description;
        description.mask = LayerMask{};
        description.mask->effects = {effect};
        gpu_append_mask_effects(stage.rows, description, set, tile);
        result.push_back(std::move(stage));
      }
      else {
        TextureLayer description;
        description.mask = LayerMask{};
        description.mask->effects = {effect};
        GpuMaskStage stage;
        stage.kind = GpuMaskStage::Points;
        stage.id = effect.id.toString().toStdString();
        gpu_append_mask_effects(stage.rows, description, set, tile);
        if (effect.kind == MaskEffectKind::Generator) {
          if (generator_has_anchors(effect)) {
            stage.generator = effect;
            stage.comparison_layer = layer.id;
          }
          if (effect.triplanar_grunge || effect.surface_fallback) {
            stage.geometry = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                                   set.projection_geometry;
            stage.spatial = GpuLayerParameters{};
            gpu_generator_surface_parameters(*stage.spatial, stage.geometry.get());
          }
        }
        result.push_back(std::move(stage));
      }
    }
    for (auto &stage : result)
      stage.owner_key = key(layer);
    return result;
  }

 public:
  GpuMaskBuilder(const TextureSet &source, int number) : set(source), tile(number) {}
  std::vector<GpuMaskStage> build(const TextureLayer &layer, bool force = false)
  {
    if (!force && !gpu_mask_needs_stages(layer))
      return {};
    if (!cache.contains(layer.id))
      cache[layer.id] = std::make_shared<const std::vector<GpuMaskStage>>(build_uncached(layer));
    return *cache.at(layer.id);
  }
};
// Visit only the prefix exposed by an anchor for dirty propagation. Later
// producer effects cannot invalidate a consumer of an earlier anchor.
template<class Visit>
void gpu_visit_mask_stage(const GpuMaskStage &stage, Visit &&visit, bool full = false)
{
  visit(stage);
  if (stage.anchor_stages)
    for (const auto &source : *stage.anchor_stages) {
      gpu_visit_mask_stage(source, visit, full);
      if (!full && source.id == stage.anchor_id)
        break;
    }
}
}  // namespace bts
