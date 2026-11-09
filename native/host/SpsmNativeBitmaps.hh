#pragma once
#include "SpsmBitmapLowering.hh"
namespace bts {
// Direct, independent embedded image bindings. The reader enforces image-only
// resource paths, byte extents, budgets and unchanged source identity.
inline void hydrate_spsm_native_bitmaps(PreparedSpsm &prepared,
                                        const SpsmBitmapLoweringPolicy &policy,
                                        const SpsmBitmapResolver &resolve,
                                        const SpsmBitmapUse &use)
{
  if (!prepared.source || !prepared.source->primary_preset || !resolve ||
      policy.pixels.bytes == SpsmByteOrder::Unknown ||
      policy.pixels.rows == SpsmRowOrder::Unknown ||
      policy.pixels.alpha != SpsmAlpha::DataComponents ||
      policy.uv != SpsmBitmapUV::Projection0CenteredRepeatFactors ||
      policy.sampling != SpsmBitmapSampling::HQRepeat)
    return;
  const auto pi = *prepared.source->primary_preset;
  const auto &p = prepared.source->source().presets.at(pi);
  const auto &ir = prepared.source->presets.at(pi);
  const auto field = [&](spsm::NodeId id, std::string_view key) -> std::optional<spsm::NodeId> {
    if (const auto o = std::get_if<spsm::Object>(&p.nodes.at(id).value))
      for (const auto &f : o->fields)
        if (f.name == key)
          return f.value;
    return {};
  };
  const auto bits = [&](std::optional<spsm::NodeId> id) -> std::uint64_t {
    if (!id)
      return 0;
    const auto &v = p.nodes.at(*id).value;
    if (const auto x = std::get_if<std::uint64_t>(&v))
      return *x;
    if (const auto x = std::get_if<std::int64_t>(&v); x && *x >= 0)
      return *x;
    return 0;
  };
  const auto numeric = [&](std::optional<spsm::NodeId> id, float fallback) {
    if (id)
      if (const auto x = std::get_if<spsm::FloatValues>(&p.nodes.at(*id).value);
          x && x->count == 1 && std::isfinite(x->values[0]))
        return x->values[0];
    return fallback;
  };
  const auto active = [&](std::optional<spsm::NodeId> id) {
    if (id)
      if (const auto x = std::get_if<bool>(&p.nodes.at(*id).value))
        return *x;
    return true;
  };
  const auto note = [&](spsm::NodeId id,
                        SpsmDiagnosticKind kind,
                        std::string_view code,
                        std::string_view detail) {
    prepared.diagnostics.push_back({pi, id, kind, code, detail});
    prepared.partial = true;
  };
  const auto mapping = [&](const spsm::semantic::Action &a) {
    FillProjection result;
    result.depth_culling = false;
    result.alpha_mask.fill(false);
    if (bits(field(a.source, "projection")) != 0)
      throw spsm::Error("Native bitmap mapping requires UV projection");
    auto uv = field(a.source, "uvTransformation");
    if (!uv)
      uv = field(a.source, "uvTransformation0");
    if (uv) {
      if (const auto scale = field(*uv, "scaleFactors")) {
        const auto x = std::get_if<spsm::FloatValues>(&p.nodes.at(*scale).value);
        if (!x || x->count != 2)
          throw spsm::Error("Invalid bitmap UV scale");
        for (int c = 0; c < 2; ++c) {
          if (!std::isfinite(x->values[c]))
            throw spsm::Error("Invalid bitmap UV scale");
          result.scale[c] = x->values[c];
        }
      }
      result.rotation = numeric(field(*uv, "rotation"), 0) * 180 / std::numbers::pi;
      if (const auto offset = field(*uv, "translation")) {
        const auto x = std::get_if<spsm::FloatValues>(&p.nodes.at(*offset).value);
        if (!x || x->count != 2)
          throw spsm::Error("Invalid bitmap UV translation");
        for (int c = 0; c < 2; ++c)
          result.offset[c] = x->values[c];
      }
    }
    result.linked = result.scale[0] == result.scale[1];
    validate_fill_projection(result);
    return result;
  };
  std::uint64_t retained = 0;
  std::set<const void *> pixel_owners, raw_owners, record_owners;
  const auto load = [&](const auto &descriptor) {
    const auto image = resolve(descriptor, policy.pixels, {});
    if (!image || image->policy != policy.pixels ||
        image->source.source != descriptor.resource.source ||
        image->source.dataset.path != descriptor.resource.dataset.path || !image->texture.pixels)
      throw spsm::Error("Native bitmap resolver returned incompatible provenance or pixels");
    const auto account = [&](const void *owner, std::uint64_t bytes, auto &owners) {
      if (owner && owners.insert(owner).second) {
        retained = resource_sum(retained, bytes);
      }
    };
    account(image->texture.pixels.get(),
            resource_product(image->texture.pixels->size(), sizeof(FillTexture::Pixel)),
            pixel_owners);
    if (image->original.bytes)
      account(image->original.bytes.get(), image->original.bytes->size(), raw_owners);
    if (image->original.encoded && image->original.encoded->record)
      account(image->original.encoded->record.get(),
              image->original.encoded->record->size(),
              record_owners);
    return image;
  };
  prepared.bitmap_color_transform = use.color;
  for (const auto &src : ir.layers) {
    const auto binding = std::find_if(prepared.layers.begin(),
                                      prepared.layers.end(),
                                      [&](const auto &b) { return b.source == src.source; });
    if (binding == prepared.layers.end())
      continue;
    auto layer = std::find_if(prepared.material.set.layers.begin(),
                              prepared.material.set.layers.end(),
                              [&](const auto &l) { return l.id == binding->native_layer; });
    if (layer == prepared.material.set.layers.end())
      continue;
    std::vector<std::size_t> content_todo(src.actions.rbegin(), src.actions.rend());
    while (!src.group && !content_todo.empty()) {
      const auto index = content_todo.back();
      content_todo.pop_back();
      const auto &a = ir.actions.at(index);
      if (!active(a.enabled))
        continue;
      for (auto it = a.children.rbegin(); it != a.children.rend(); ++it)
        content_todo.push_back(*it);
      if (a.kind != spsm::semantic::ActionKind::Fill)
        continue;
      for (const auto id : a.sources) {
        const auto source = std::find_if(
            ir.sources.begin(), ir.sources.end(), [&](const auto &x) { return x.source == id; });
        if (source == ir.sources.end() || source->kind != spsm::semantic::Source::Kind::Bitmap ||
            !source->bitmap)
          continue;
        const auto uri = field(*source->bitmap, "urlToBitmapRes");
        if (!uri)
          continue;
        const auto reference = std::find_if(prepared.source->source().references.begin(),
                                            prepared.source->source().references.end(),
                                            [&](const auto &r) {
                                              return r.preset == pi && r.source == *uri &&
                                                     r.field == "urlToBitmapRes";
                                            });
        if (reference == prepared.source->source().references.end() ||
            !reference->resource_dataset.ends_with(".image"))
          continue;
        constexpr std::array<std::uint64_t, 6> channels{1, 8192, 128, 4194304, 2, 8388608};
        auto selected = bits(field(id, "channelTypes")) &
                        (field(a.source, "channelTypes") ? bits(field(a.source, "channelTypes")) :
                                                           ~std::uint64_t(0));
        if (!use.color)
          selected &= ~std::uint64_t(1);
        if (!use.roughness)
          selected &= ~std::uint64_t(128);
        if (!selected)
          continue;
        try {
          const auto descriptor = spsm::bitmap::describe_image(
              prepared.source->source(), reference->resource_dataset, {});
          const auto image = load(descriptor);
          if (!image)
            throw spsm::Error("Native bitmap resolver returned no image");
          auto projection = mapping(a);
          projection.texture = image->binding();
          projection.alpha_mask[0] = false;
          prepared.bitmap_content_sources.insert_or_assign(id, projection);
          prepared.bitmap_content_channels.insert_or_assign(id, selected);
          const auto initial = prepared.initial_fill_actions.find(src.source);
          if (initial == prepared.initial_fill_actions.end() || initial->second != a.source) {
            note(
                id,
                SpsmDiagnosticKind::Assumption,
                "native-ordered-bitmap",
                "Embedded bitmap retains its own centered UV mapping at its source Fill action, with raw data/ignored alpha and named Color interpretation");
            continue;
          }
          projection.texture.reset();
          if (fill_has_texture(layer->fill))
            projection = layer->fill;
          bool any = false;
          for (int c : {0, 1, 2, 3, 4, 5})
            if (selected & channels[c]) {
              if (c == 0 && !use.color)
                continue;
              if (c == 2 && !use.roughness)
                continue;
              if (fill_channel_texture(projection, c))
                continue;
              fill_channel_texture(projection, c) = image->binding();
              projection.alpha_mask[c] = false;
              const float source_op = std::clamp(numeric(source->opacity, 1), 0.f, 1.f);
              float action_op = 1;
              for (const auto &b : a.blending)
                if (bits(b.channel_types) & channels[c])
                  action_op = std::clamp(numeric(b.opacity, 1), 0.f, 1.f);
              if (c < 5) {
                layer->channels[c] = true;
                layer->opacity[c] *= source_op * action_op;
              }
              else {
                layer->ao_enabled = true;
                layer->ao_opacity *= source_op * action_op;
                prepared.material.set.ao_channel = true;
              }
              if (c == 0) {
                layer->values.base_color = {1, 1, 1, 1};
                LevelsEffect boundary;
                boundary.kind = ContentEffectKind::LinearColorTransform;
                boundary.channel = 0;
                boundary.linear_color = use.color;
                std::vector<LevelsEffect> chain;
                for (const auto ai : src.actions) {
                  const auto &effect = ir.actions.at(ai);
                  if (effect.kind == spsm::semantic::ActionKind::Levels &&
                      bits(field(effect.source, "channelTypes")) == 1 && active(effect.enabled))
                  {
                    try {
                      chain.push_back(parse_spsm_native_rgb_levels(p, effect));
                    }
                    catch (const SpsmLevelsUnsupported &) {
                    }
                  }
                }
                chain.push_back(std::move(boundary));
                for (const auto &effect : layer->effects)
                  if (effect.channel != 0)
                    chain.push_back(effect);
                layer->effects = std::move(chain);
              }
              if (c == 3) {
                prepared.material.set.channels[3] = true;
                note(
                    id,
                    SpsmDiagnosticKind::Assumption,
                    "native-internal-normal-bitmap",
                    "Embedded tangent Normal bitmap retained internally as encoded RGB, with native normalization and destination tangent convention; source handedness remains unverified");
              }
              if (c == 4)
                projection.bump_bias = .5f;
              any = true;
            }
          if (any) {
            layer->fill = std::move(projection);
            layer->visible = active(src.enabled);
            note(
                id,
                SpsmDiagnosticKind::Assumption,
                "native-independent-bitmap",
                "Direct embedded bitmap bound independently of unsupported sibling actions and masks, using native raw data/ignored alpha, centered repeat UVs and named linear RGB interpretation");
          }
        }
        catch (const spsm::Error &) {
          note(
              id,
              SpsmDiagnosticKind::Omission,
              "native-bitmap-unavailable",
              "Embedded image format or UV mapping cannot be hydrated; other native layer contributions and authored visibility are preserved");
        }
      }
      // Later Fill actions are retained as independent sources, never replacing the layer
      // projection.
    }
    if (policy.mask == SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA) {
      std::vector<std::size_t> todo(src.mask_actions.rbegin(), src.mask_actions.rend());
      while (!todo.empty()) {
        const auto index = todo.back();
        todo.pop_back();
        const auto &a = ir.actions.at(index);
        if (!active(a.enabled))
          continue;
        for (auto it = a.children.rbegin(); it != a.children.rend(); ++it)
          todo.push_back(*it);
        if (a.kind != spsm::semantic::ActionKind::Fill)
          continue;
        for (const auto id : a.sources) {
          const auto source = std::find_if(
              ir.sources.begin(), ir.sources.end(), [&](const auto &v) { return v.source == id; });
          if (source == ir.sources.end() || source->kind != spsm::semantic::Source::Kind::Bitmap ||
              !source->bitmap)
            continue;
          const auto uri = field(*source->bitmap, "urlToBitmapRes");
          if (!uri)
            continue;
          const auto reference = std::find_if(prepared.source->source().references.begin(),
                                              prepared.source->source().references.end(),
                                              [&](const auto &r) {
                                                return r.preset == pi && r.source == *uri &&
                                                       r.field == "urlToBitmapRes";
                                              });
          if (reference == prepared.source->source().references.end() ||
              !reference->resource_dataset.ends_with(".image"))
            continue;
          try {
            const auto descriptor = spsm::bitmap::describe_image(
                prepared.source->source(), reference->resource_dataset, {});
            const auto image = load(descriptor);
            if (!image)
              throw spsm::Error("No native mask bitmap");
            auto projection = mapping(a);
            projection.texture = image->binding();
            projection.alpha_mask[0] = false;
            prepared.bitmap_mask_sources.insert_or_assign(id, std::move(projection));
            note(
                id,
                SpsmDiagnosticKind::Assumption,
                "native-independent-bitmap-mask",
                "Direct embedded mask image uses native raw R, ignored alpha and centered repeat mapping; ordered native Fill/Levels/group semantics remain editable");
          }
          catch (const spsm::Error &) {
            note(
                id,
                SpsmDiagnosticKind::Omission,
                "native-mask-bitmap-unavailable",
                "Mask bitmap format or mapping could not be hydrated; its unsupported subaction is omitted independently");
          }
        }
      }
    }
  }
}
}  // namespace bts
