// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmBitmapLowering.hh"
#include "ResourceAvailability.hh"
#include "SpsmBitmapFields.hh"
#include <set>
namespace bts {
namespace {
namespace s = spsm;
namespace sem = spsm::semantic;
struct Unsupported {};
struct LevelsPolicyRequired {
  s::NodeId source;
};
struct MaskPolicyRequired {
  s::NodeId source;
};
struct Lower {
  const PreparedSpsm &original;
  const SpsmBitmapLoweringPolicy &policy;
  const SpsmBitmapResolver &resolve;
  const SpsmBitmapUse &use;
  PreparedSpsm result;
  std::size_t pi;
  const sem::PresetInspection &ir;
  const s::Preset &p;
  std::uint64_t used = 0;
  std::set<const void *> retained_images, retained_pixels, retained_raw, retained_records;
  Lower(const PreparedSpsm &o,
        const SpsmBitmapLoweringPolicy &v,
        const SpsmBitmapResolver &r,
        const SpsmBitmapUse &channels)
      : original(o),
        policy(v),
        resolve(r),
        use(channels),
        pi(primary(o)),
        ir(o.source->presets.at(pi)),
        p(o.source->source().presets.at(pi))
  {
    // Material pixels/recipes and retained source are shared, not evaluated.
    // This unit accepts only its preparer's untouched Fill/Folder metadata
    // subset.
    add(resource_product(o.material.set.layers.size(), sizeof(TextureLayer) * 2));
    add(resource_product(o.layers.size(), sizeof(SpsmLayerBinding) * 2));
    add(resource_product(o.actions.size(), sizeof(SpsmActionBinding) * 2));
    add(resource_product(o.diagnostics.size(), sizeof(SpsmImportDiagnostic) * 2));
    add(resource_product(resource_sum(ir.layers.size(), ir.sources.size()), sizeof(void *) * 8));
    for (const auto &l : o.material.set.layers) {
      add(resource_product(l.name.size(), 2));
      if (!l.effects.empty() || (l.mask && !l.mask->effects.empty()) ||
          !l.pixels.tiles().empty() || l.strokes || l.instance || l.quick_overlay)
        throw s::Error("Bitmap enhancement requires unchanged SPSM preparation metadata");
    }
    if (use.color) {
      use.color->validate();
      add(sizeof(LinearColorTransform) + resource_product(use.color->input_space.size(), 4));
    }
    result = o;
  }
  static std::size_t primary(const PreparedSpsm &o)
  {
    if (!o.source || !o.source->primary_preset ||
        *o.source->primary_preset >= o.source->presets.size() ||
        *o.source->primary_preset >= o.source->source().presets.size())
      throw s::Error("Bitmap preparation has no retained primary stream");
    return *o.source->primary_preset;
  }
  void add(std::uint64_t bytes)
  {
    used = resource_sum(used, bytes);
  }
  const s::Node &node(s::NodeId id) const
  {
    if (id >= p.nodes.size())
      throw s::Error("Bitmap source node out of range");
    return p.nodes[id];
  }
  std::optional<s::NodeId> field(s::NodeId id, std::string_view key) const
  {
    const auto *o = std::get_if<s::Object>(&node(id).value);
    if (!o)
      throw s::Error("Bitmap source object has invalid type");
    std::optional<s::NodeId> found;
    for (const auto &f : o->fields)
      if (f.name == key) {
        if (found)
          throw s::Error("Duplicate bitmap source field");
        found = f.value;
      }
    return found;
  }
  std::uint64_t bits(std::optional<s::NodeId> id) const
  {
    if (!id)
      throw Unsupported{};
    const auto &v = node(*id).value;
    if (auto *n = std::get_if<std::uint64_t>(&v))
      return *n;
    if (auto *n = std::get_if<std::int64_t>(&v); n && *n >= 0)
      return *n;
    if (auto *n = std::get_if<s::IntValues>(&v); n && n->count == 1 && n->values[0] >= 0)
      return n->values[0];
    throw Unsupported{};
  }
  double number(std::optional<s::NodeId> id) const
  {
    if (!id)
      throw Unsupported{};
    const auto &v = node(*id).value;
    double x;
    if (auto *n = std::get_if<s::FloatValues>(&v); n && n->count == 1)
      x = n->values[0];
    else
      x = double(bits(id));
    if (!std::isfinite(x))
      throw s::Error("Nonfinite bitmap source scalar");
    return x;
  }
  float unit(std::optional<s::NodeId> id, float fallback = 1) const
  {
    if (!id)
      return fallback;
    const auto x = number(id);
    if (x < 0 || x > 1)
      throw s::Error("Bitmap source coverage outside [0,1]");
    return float(x);
  }
  bool boolean(std::optional<s::NodeId> id, bool fallback = true) const
  {
    if (!id)
      return fallback;
    const auto *v = std::get_if<bool>(&node(*id).value);
    if (!v)
      throw s::Error("Invalid bitmap source boolean");
    return *v;
  }
  std::array<double, 2> pair(std::optional<s::NodeId> id) const
  {
    if (!id)
      throw Unsupported{};
    auto *v = std::get_if<s::FloatValues>(&node(*id).value);
    if (!v || v->count != 2)
      throw Unsupported{};
    if (!std::isfinite(v->values[0]) || !std::isfinite(v->values[1]))
      throw s::Error("Nonfinite bitmap UV transform");
    return {v->values[0], v->values[1]};
  }
  void note(s::NodeId source,
            SpsmDiagnosticKind kind,
            std::string_view code,
            std::string_view detail)
  {
    add(sizeof(SpsmImportDiagnostic) * 2);
    result.diagnostics.push_back({pi, source, kind, code, detail});
    result.partial = true;
    result.appearance_verified = false;
  }
  SpsmLayerBinding &binding(s::NodeId id)
  {
    SpsmLayerBinding *found = nullptr;
    for (auto &b : result.layers)
      if (b.source == id) {
        if (found)
          throw s::Error("Duplicate native layer binding");
        found = &b;
      }
    if (!found)
      throw s::Error("Missing native layer binding");
    return *found;
  }
  TextureLayer &native(const QUuid &id)
  {
    TextureLayer *found = nullptr;
    for (auto &l : result.material.set.layers)
      if (l.id == id) {
        if (found)
          throw s::Error("Duplicate native layer ID");
        found = &l;
      }
    if (!found)
      throw s::Error("Missing prepared native layer");
    return *found;
  }
  bool policy_known() const
  {
    return (policy.pixels.bytes == SpsmByteOrder::LittleEndian ||
            policy.pixels.bytes == SpsmByteOrder::BigEndian) &&
           (policy.pixels.rows == SpsmRowOrder::FirstRowIsV0 ||
            policy.pixels.rows == SpsmRowOrder::FirstRowIsV1) &&
           policy.pixels.alpha == SpsmAlpha::DataComponents && policy.pixels.raw &&
           policy.pixels.input_space.empty() &&
           policy.uv == SpsmBitmapUV::Projection0CenteredRepeatFactors &&
           policy.sampling == SpsmBitmapSampling::HQRepeat;
  }
  void geometry(const sem::Layer &l) const
  {
    for (auto ref : l.geometry) {
      const auto &f = sem::source_field(*original.source, pi, ref);
      if (f.name == "enabledMeshList" || f.name == "enabledUVTileList") {
        auto *a = std::get_if<s::Array>(&node(f.value).value);
        if (!a || !a->elements.empty())
          throw Unsupported{};
      }
      else if (f.name == "enabledMeshDefault" || f.name == "enabledUVTileDefault") {
        if (!boolean(f.value))
          throw Unsupported{};
      }
      else if (f.name == "geometryMaskType") {
        if (bits(f.value) != 0)
          throw Unsupported{};
      }
      else
        boolean(f.value);
    }
  }
  void masks(const sem::Layer &l) const
  {
    unit(l.mask_initial);
    if (boolean(l.mask_enabled)) {
      for (auto ai : l.mask_actions) {
        const auto &a = ir.actions.at(ai);
        if (boolean(a.enabled))
          throw Unsupported{};
      }
    }
  }
  std::pair<float, LayerBlend> blending(const std::vector<sem::Blending> &rows,
                                        bool initial,
                                        std::uint64_t channel = 128) const
  {
    float opacity = 1;
    LayerBlend mode = LayerBlend::Normal;
    bool found = false;
    for (const auto &r : rows) {
      try {
        spsm_bitmap_detail::Fields(p).blend(r.source);
      }
      catch (const SpsmLevelsUnsupported &) {
        throw Unsupported{};
      }
      if (!(bits(r.channel_types) & channel))
        continue;
      if (found)
        throw Unsupported{};
      found = true;
      const auto raw = bits(r.source_mode);
      switch (raw) {
        case 0:
          mode = LayerBlend::Disabled;
          break;
        case 1:
          mode = LayerBlend::Replace;
          break;
        case 2:
          mode = LayerBlend::Normal;
          break;
        case 4:
          mode = LayerBlend::Multiply;
          break;
        case 10:
          mode = LayerBlend::LinearDodge;
          break;
        default:
          throw Unsupported{};
      }
      if (initial && raw != 1 && raw != 2)
        throw Unsupported{};
      opacity = unit(r.opacity);
    }
    return {opacity, mode};
  }
  const sem::Source *bitmap_source(const sem::Layer &l, std::uint64_t channel) const
  {
    const sem::Source *found = nullptr;
    for (auto ai : l.actions) {
      const auto &a = ir.actions.at(ai);
      for (auto id : a.sources) {
        const sem::Source *src = nullptr;
        for (const auto &v : ir.sources)
          if (v.source == id) {
            if (src)
              throw s::Error("Duplicate semantic source");
            src = &v;
          }
        if (!src)
          throw s::Error("Missing semantic source");
        if (src->kind == sem::Source::Kind::Bitmap && (bits(field(id, "channelTypes")) & channel))
        {
          if (found)
            throw Unsupported{};
          found = src;
        }
      }
    }
    return found;
  }
  struct Content {
    const sem::Action *fill = nullptr;
    std::vector<LevelsEffect> levels;
    std::vector<s::NodeId> level_sources;
  };
  Content content(const sem::Layer &l, const sem::Source *color, const sem::Source *rough)
  {
    Content result;
    const sem::Action *fill = nullptr;
    bool bound = false;
    for (auto ai : l.actions) {
      const auto &a = ir.actions.at(ai);
      if (!boolean(a.enabled))
        continue;
      if (!a.children.empty())
        throw Unsupported{};
      if (a.kind == sem::ActionKind::Fill) {
        if (fill)
          throw Unsupported{};
        fill = &a;
        for (auto [channel, source] :
             {std::pair<std::uint64_t, const sem::Source *>{1, color}, {128, rough}})
        {
          if (!source)
            continue;
          std::size_t count = 0;
          bool found = false;
          for (auto id : a.sources)
            if (bits(field(id, "channelTypes")) & channel) {
              ++count;
              found = id == source->source;
            }
          if (count != 1 || !found || !(bits(field(a.source, "channelTypes")) & channel))
            throw Unsupported{};
        }
        bound = true;
      }
      else if (a.kind == sem::ActionKind::Levels) {
        const auto channels = bits(field(a.source, "channelTypes"));
        try {
          validate_spsm_levels_action(p, a);
          if (channels != 1 && channels != 128)
            throw Unsupported{};
          if ((channels == 1 && !color) || (channels == 128 && !rough))
            continue;
          if (!fill)
            throw Unsupported{};
          if (channels == 128 && policy.levels != SpsmLevelsPolicy::NativeScalarLevels)
            throw LevelsPolicyRequired{a.source};
          add(sizeof(LevelsEffect) * 2 + sizeof(s::NodeId) * 2);
          result.levels.push_back(channels == 1 ? parse_spsm_native_rgb_levels(p, a) :
                                                  parse_spsm_scalar_levels(p, a, policy.levels));
          result.level_sources.push_back(a.source);
        }
        catch (const SpsmLevelsUnsupported &unsupported) {
          note(
              unsupported.source,
              SpsmDiagnosticKind::Omission,
              "bitmap-levels-unsupported",
              "Selected Levels source field or dependency has no accepted native policy; original source remains retained and no selected bitmap channel is hydrated");
          throw Unsupported{};
        }
      }
      else
        throw Unsupported{};
    }
    if (!fill || !bound)
      throw Unsupported{};
    result.fill = fill;
    return result;
  }
  struct MaskChain {
    const sem::Action *group = nullptr, *fill = nullptr, *levels = nullptr;
    const sem::Source *source = nullptr;
    FillProjection mapping;
    LevelsEffect curve;
    explicit operator bool() const
    {
      return fill;
    }
  };
  MaskChain mask_chain(const sem::Layer &l) const
  {
    if (!boolean(l.mask_enabled)) {
      masks(l);
      return {};
    }
    bool active = false;
    for (auto i : l.mask_actions)
      active |= boolean(ir.actions.at(i).enabled);
    if (!active) {
      masks(l);
      return {};
    }
    if (policy.mask != SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA)
      throw MaskPolicyRequired{l.source};
    if (l.mask_actions.size() != 3 || unit(l.mask_initial) != 0)
      throw Unsupported{};
    const auto &g = ir.actions.at(l.mask_actions[0]), &f = ir.actions.at(l.mask_actions[1]),
               &v = ir.actions.at(l.mask_actions[2]);
    if (g.kind != sem::ActionKind::Group || f.kind != sem::ActionKind::Fill ||
        v.kind != sem::ActionKind::Levels || !boolean(g.enabled) || !boolean(f.enabled) ||
        !boolean(v.enabled) || !g.children.empty() || !g.sources.empty() || !f.children.empty() ||
        f.sources.size() != 1 || !f.blending.empty() || g.blending.size() != 1 ||
        bits(field(f.source, "channelTypes")) != 0)
      throw Unsupported{};
    try {
      spsm_bitmap_detail::Fields fields(p);
      fields.layer(l.source, false);
      const std::array mask_nodes{g.source, f.source, v.source};
      fields.stack(l.source, "maskActions", mask_nodes);
      if (l.actions.size() != 1)
        throw Unsupported{};
      const std::array content_nodes{ir.actions.at(l.actions.front()).source};
      fields.stack(l.source, "actions", content_nodes);
      fields.type(g.source, "DataActionGroup");
      fields.fields(g.source,
                    {"channelTypes",
                     "enabled",
                     "label",
                     "perChannelBlending",
                     "selection",
                     "subStack",
                     "tags",
                     "uid"});
      fields.zero_selection(g.source);
      fields.metadata(g.source);
      if (bits(field(g.source, "channelTypes")) != 0)
        throw Unsupported{};
      const auto stack = fields.required(g.source, "subStack");
      fields.type(stack, "DataStackActions");
      fields.fields(stack, {"items", "uid"});
      fields.metadata(stack);
      const auto items = std::get_if<s::Array>(&node(fields.required(stack, "items")).value);
      if (!items || !items->elements.empty())
        throw Unsupported{};
      const auto &blend = g.blending.front();
      fields.blend(blend.source);
      if (bits(blend.channel_types) != UINT64_MAX || bits(blend.source_mode) != 25 ||
          unit(blend.opacity) != 1)
        throw Unsupported{};
      fields.fill(f.source);
      if (bits(field(v.source, "channelTypes")) != 0)
        throw Unsupported{};
      MaskChain result;
      result.group = &g;
      result.fill = &f;
      result.levels = &v;
      result.curve = parse_spsm_scalar_levels(
          p, v, SpsmLevelsPolicy::NativeScalarLevels, SpsmScalarDomain::MaskAuxiliary0);
      for (const auto &source : ir.sources)
        if (source.source == f.sources[0]) {
          if (result.source)
            throw s::Error("Duplicate mask bitmap source");
          result.source = &source;
        }
      if (!result.source || result.source->kind != sem::Source::Kind::Bitmap ||
          bits(field(result.source->source, "channelTypes")) != 1 ||
          unit(result.source->opacity) != 1)
        throw Unsupported{};
      fields.bitmap(result.source->source);
      result.mapping = mapping(f);
      return result;
    }
    catch (const SpsmLevelsUnsupported &) {
      throw Unsupported{};
    }
  }
  FillProjection mapping(const sem::Action &a) const
  {
    try {
      spsm_bitmap_detail::Fields(p).fill(a.source);
    }
    catch (const SpsmLevelsUnsupported &) {
      throw Unsupported{};
    }
    if (bits(field(a.source, "projection")) != 0)
      throw Unsupported{};
    if (const auto sel = field(a.source, "selection")) {
      const auto *v = std::get_if<s::Blob>(&node(*sel).value);
      if (!v || v->size)
        throw Unsupported{};
    }
    // UV culling is explicitly assumed inactive. Nonzero/unknown projection is
    // rejected above; no spatial transforms or source wrap/filter enum casts.
    for (const auto key : {"backfaceCulling", "depthCulling", "cylinderHideCaps"})
      boolean(field(a.source, key), false);
    const auto uv = field(a.source, "uvTransformation");
    if (!uv)
      throw Unsupported{};
    const auto *o = std::get_if<s::Object>(&node(*uv).value);
    if (!o || o->class_name != "Data2DTransformation")
      throw Unsupported{};
    if (bits(field(*uv, "scaleMode")) != 0 ||
        boolean(field(*uv, "physicalSizeBoundedByScaleFactors"), false) ||
        number(field(*uv, "rotation")) != 0 ||
        pair(field(*uv, "translation")) != std::array<double, 2>{0, 0} ||
        pair(field(*uv, "GUIPivot")) != std::array<double, 2>{.5, .5})
      throw Unsupported{};
    FillProjection out;
    out.scale = pair(field(*uv, "scaleFactors"));
    out.linked = out.scale[0] == out.scale[1];
    out.depth_culling = false;
    out.wrap = 0;
    out.filtering = 0;  // Explicit HQRepeat; other policies fail before mapping.
    try {
      validate_fill_projection(out);
    }
    catch (const std::invalid_argument &) {
      throw Unsupported{};
    }
    return out;
  }
  s::bitmap::Descriptor descriptor(const sem::Source &source) const
  {
    try {
      spsm_bitmap_detail::Fields(p).bitmap(source.source);
    }
    catch (const SpsmLevelsUnsupported &) {
      throw Unsupported{};
    }
    if (!source.bitmap)
      throw Unsupported{};
    const auto bitmap = *source.bitmap;
    const auto *bitmap_object = std::get_if<s::Object>(&node(bitmap).value);
    if (!bitmap_object || bitmap_object->class_name != "DataBitmap")
      throw Unsupported{};
    const auto uri_id = field(bitmap, "urlToBitmapRes");
    if (!uri_id)
      throw Unsupported{};
    const auto *uri = std::get_if<std::string>(&node(*uri_id).value);
    if (!uri || uri->empty())
      throw Unsupported{};
    const s::Reference *ref = nullptr;
    for (const auto &r : original.source->source().references)
      if (r.preset == pi && r.source == *uri_id && r.field == "urlToBitmapRes") {
        if (ref)
          throw s::Error("Duplicate bitmap resource reference");
        ref = &r;
      }
    if (!ref || ref->uri != *uri || !ref->resource_dataset.starts_with("/resources/"))
      throw Unsupported{};
    // Independently bind the URL version key to the inventory descriptor. Never
    // turn an authoring-machine URI into a filesystem path.
    auto query = std::string_view(*uri);
    const auto question = query.find('?');
    if (question == query.npos)
      throw Unsupported{};
    query.remove_prefix(question + 1);
    std::optional<std::string_view> version;
    while (!query.empty()) {
      const auto end = query.find('&');
      const auto item = query.substr(0, end);
      if (item.starts_with("version=")) {
        if (version)
          throw Unsupported{};
        version = item.substr(8);
      }
      if (end == query.npos)
        break;
      query.remove_prefix(end + 1);
    }
    if (!version || *version != std::string_view(ref->resource_dataset).substr(11))
      throw Unsupported{};
    return s::bitmap::describe_image(original.source->source(), ref->resource_dataset, {});
  }
  void validate_resolved(const s::bitmap::Descriptor &d,
                         const std::shared_ptr<const SpsmBitmapTexture> &v) const
  {
    if (!v || v->policy != policy.pixels || v->source.source != d.resource.source ||
        v->source.dataset.path != d.resource.dataset.path ||
        v->source.dataset.size != d.resource.dataset.size ||
        v->source.stamp.file_bytes != d.resource.stamp.file_bytes ||
        v->source.stamp.modified != d.resource.stamp.modified || !v->original.encoded ||
        !v->original.encoded->header || !v->original.encoded->record || !v->original.bytes ||
        v->encoded_sha256.size() != 64)
      throw s::Error("SPSM bitmap resolver returned different provenance/policy");
    const auto &h = *v->original.encoded->header;
    if (!d.header || !h.encoded || !d.header->encoded || *h.encoded != *d.header->encoded ||
        h.record_bytes != d.header->record_bytes ||
        v->original.encoded->record->size() != h.record_bytes ||
        v->original.bytes->size() != h.raw_bytes || v->texture.width != h.width ||
        v->texture.height != h.height || !v->texture.raw || !v->texture.source_space.empty() ||
        !v->texture.pixels || v->texture.tiles)
      throw s::Error("SPSM bitmap resolver returned incompatible native storage");
    v->texture.validate();
    if (!v->texture.range_valid || !std::isfinite(v->texture.minimum) ||
        !std::isfinite(v->texture.maximum))
      throw s::Error("SPSM Roughness bitmap has invalid range metadata");
  }
  void candidate(std::size_t li, const sem::Source *color, const sem::Source *rough)
  {
    const auto &source = *(color ? color : rough);
    const auto &l = ir.layers.at(li);
    auto &b = binding(l.source);
    auto &out = native(b.native_layer);
    if (!b.disabled || out.kind != LayerKind::Fill || fill_has_texture(out.fill) ||
        fill_has_anchors(out.fill) || out.fill.native_material || out.fill.atom)
      throw Unsupported{};
    if (!policy_known() || !resolve) {
      note(source.source,
           SpsmDiagnosticKind::Omission,
           "bitmap-policy-required",
           "Selected bitmap channels retained disabled: explicit raw byte/row/data-alpha, "
           "centered UV and proven HQRepeat policy plus resolver required; "
           "Sharp/Nearest are unsupported");
      return;
    }
    if (color) {
      try {
        spsm_bitmap_detail::Fields fields(p);
        fields.layer(l.source, false);
        std::vector<s::NodeId> actions;
        actions.reserve(l.actions.size());
        for (auto index : l.actions)
          actions.push_back(ir.actions.at(index).source);
        fields.stack(l.source, "actions", actions);
      }
      catch (const SpsmLevelsUnsupported &) {
        throw Unsupported{};
      }
    }
    geometry(l);
    MaskChain mask;
    if (color)
      masks(l);
    else
      mask = mask_chain(l);
    // Disabled source branches stay invisible; unsupported ancestors cannot be
    // repaired by enabling a child. Bound traversal detects malformed cycles.
    std::set<std::size_t> parents;
    auto parent = l.parent;
    while (parent) {
      if (!parents.insert(*parent).second || *parent == li)
        throw s::Error("Cyclic bitmap source ancestry");
      const auto &ancestor = ir.layers.at(*parent);
      auto &pb = binding(ancestor.source);
      if (!ancestor.group || pb.disabled || (color && !native(pb.native_layer).channels[0]) ||
          (rough && !native(pb.native_layer).channels[2]))
        throw Unsupported{};
      geometry(ancestor);
      if (mask || color) {
        try {
          spsm_bitmap_detail::Fields fields(p);
          fields.layer(ancestor.source, true);
          fields.stack(ancestor.source, "actions", {});
          fields.stack(ancestor.source, "maskActions", {});
        }
        catch (const SpsmLevelsUnsupported &) {
          throw Unsupported{};
        }
      }
      masks(ancestor);
      if (boolean(ancestor.mask_enabled) && unit(ancestor.mask_initial) == 0)
        note(
            ancestor.source,
            SpsmDiagnosticKind::Assumption,
            "bitmap-zero-ancestor-mask",
            "Retained source maskEnabled=true and maskInitial=0 with no active mask actions; native ancestor coverage remains zero under the reported initial-mask/Boolean policy. No source appearance assertion or unmasking");
      parent = ancestor.parent;
    }
    auto chain = content(l, color, rough);
    if (mask && (l.actions.size() != 1 || !chain.levels.empty()))
      throw Unsupported{};
    const auto &a = *chain.fill;
    auto projection = mapping(a);
    std::array<float, 3> opacities{};
    std::array<LayerBlend, 3> modes{};
    for (auto [channel, src] : {std::pair<int, const sem::Source *>{0, color}, {2, rough}}) {
      if (!src)
        continue;
      const auto [op, mode] = blending(l.blending, false, channel == 0 ? 1 : 128);
      const auto [fill_op, fill_mode] = blending(a.blending, true, channel == 0 ? 1 : 128);
      if (mode == LayerBlend::Disabled)
        throw Unsupported{};
      opacities[channel] = op * fill_op * unit(src->opacity);
      modes[channel] = mode;
    }
    if (mask) {
      const auto [fill_op, fill_mode] = blending(a.blending, true);
      if (a.sources.size() != 1 || bits(field(source.source, "channelTypes")) != 128 ||
          !a.blending.empty() || !l.blending.empty() || unit(source.opacity) != 1 ||
          fill_op != 1 || fill_mode != LayerBlend::Normal)
        throw Unsupported{};
    }

    // Admit every selected source/header before decoding either. Distinct
    // channel datasets stay distinct; only the exact same resource is shared.
    auto d = descriptor(source);
    std::optional<s::bitmap::Descriptor> rough_descriptor;
    if (color && rough)
      rough_descriptor = descriptor(*rough);
    const auto accepted_format = [](const auto &d) {
      const auto *format = d.header ? d.header->field("format") : nullptr;
      const auto *text = format ? std::get_if<s::bitmap::Text>(&format->value) : nullptr;
      if (!d.header || d.header->version != "v2" || !format || format->source_null || !text ||
          text->null || text->utf8 != "RGBA16F")
        throw Unsupported{};
    };
    accepted_format(d);
    if (rough_descriptor)
      accepted_format(*rough_descriptor);
    if (mask) {
      const auto md = descriptor(*mask.source);
      if (md.resource.dataset.path != d.resource.dataset.path ||
          md.resource.source != d.resource.source || !md.header || !d.header ||
          !md.header->encoded || !d.header->encoded || *md.header->encoded != *d.header->encoded)
        throw Unsupported{};
      add(sizeof(MaskEffect) * 4);
    }
    const auto load = [&](const auto &descriptor) {
      auto image = resolve(descriptor, policy.pixels, {});
      validate_resolved(descriptor, image);
      if (retained_pixels.insert(image->texture.pixels.get()).second)
        add(resource_product(image->texture.pixels->size(), sizeof(FillTexture::Pixel)));
      if (retained_raw.insert(image->original.bytes.get()).second)
        add(image->original.bytes->size());
      if (retained_records.insert(image->original.encoded->record.get()).second)
        add(image->original.encoded->record->size());
      if (retained_images.insert(image.get()).second)
        add(resource_sum(sizeof(SpsmBitmapTexture),
                         resource_product(image->texture.name.size(), 8)));
      return image;
    };
    auto image = load(d), rough_image = rough ? image : std::shared_ptr<const SpsmBitmapTexture>{};
    if (rough_descriptor && (rough_descriptor->resource.dataset.path != d.resource.dataset.path ||
                             rough_descriptor->resource.source != d.resource.source))
      rough_image = load(*rough_descriptor);
    if (rough && (rough_image->texture.minimum < 0 || rough_image->texture.maximum > 1))
      note(source.source,
           SpsmDiagnosticKind::Assumption,
           "bitmap-roughness-range",
           "Raw R includes values outside [0,1]; pixels retained unchanged. "
           "Native Roughness compositor clamps sampled R to [0,1], a reported "
           "destination behavior, not verified source equivalence");
    for (int channel : {0, 2})
      if (channel == 0 ? bool(color) : bool(rough)) {
        projection.alpha_mask[channel] = false;
        fill_channel_texture(projection,
                             channel) = (channel == 0 ? image : rough_image)->binding();
      }
    if (color) {
      add(sizeof(LevelsEffect) * 2);
      LevelsEffect boundary;
      boundary.channel = 0;
      boundary.kind = ContentEffectKind::LinearColorTransform;
      boundary.linear_color = use.color;
      chain.levels.push_back(std::move(boundary));
    }
    std::vector<MaskEffect> mask_effects;
    if (mask) {
      MaskEffect fill;
      fill.kind = MaskEffectKind::Fill;
      fill.opacity = 1;
      fill.blending = LayerBlend::Normal;
      fill.mapping.projection = std::move(mask.mapping);
      fill.mapping.projection.texture = image->binding();
      fill.mapping.projection.alpha_mask[0] = false;
      MaskEffect levels;
      levels.kind = MaskEffectKind::Levels;
      levels.levels = mask.curve;
      levels.levels.id = levels.id;
      mask_effects = {std::move(fill), std::move(levels)};
    }
    out.fill = std::move(projection);
    if (mask)
      out.mask->effects = std::move(mask_effects);
    out.channels.fill(false);
    out.ao_enabled = false;
    for (int channel : {0, 2})
      if (channel == 0 ? bool(color) : bool(rough)) {
        out.channels[channel] = true;
        out.opacity[channel] = opacities[channel];
        out.blending[channel] = modes[channel];
      }
    if (color)
      out.values.base_color = {1, 1, 1, 1};
    out.effects = std::move(chain.levels);
    if (color)
      (void)raw_linear_color_source(out);
    out.visible = boolean(l.enabled);
    b.disabled = false;
    bool found = false;
    for (auto &ab : result.actions)
      if (ab.source == a.source) {
        if (found || ab.native_layer != out.id)
          throw s::Error("Invalid bitmap action binding");
        ab.lowered = true;
        found = true;
      }
    if (!found)
      throw s::Error("Missing bitmap action binding");
    for (auto id : chain.level_sources) {
      bool bound = false;
      for (auto &ab : result.actions)
        if (ab.source == id) {
          if (bound || ab.native_layer != out.id)
            throw s::Error("Invalid bitmap Levels action binding");
          ab.lowered = true;
          bound = true;
        }
      if (!bound)
        throw s::Error("Missing bitmap Levels action binding");
      if (bits(field(id, "channelTypes")) == 1)
        note(
            id,
            SpsmDiagnosticKind::Assumption,
            "bitmap-native-rgb-levels-policy",
            "Explicit native per-component RGB Levels in chosen named source-linear space, before source-to-ACEScg matrix and canonical tint: reciprocal gamma, signed intermediates, clamp0 off/1 final[0,1]. Source color-space/alpha ordinals and appearance remain unverified");
      else
        note(
            id,
            SpsmDiagnosticKind::Assumption,
            "bitmap-native-scalar-levels-policy",
            "Explicit NativeScalarLevels after native Roughness sampling (sampled R first clamped [0,1], immutable raw pixels unchanged): reciprocal gamma, signed native intermediates and clamp 0 off/1 final [0,1]; no source equivalence asserted");
    }
    if (mask) {
      for (auto id : {mask.fill->source, mask.levels->source}) {
        bool found = false;
        for (auto &ab : result.actions)
          if (ab.source == id) {
            if (found || ab.native_layer != out.id)
              throw s::Error("Invalid bitmap mask action binding");
            ab.lowered = true;
            found = true;
          }
        if (!found)
          throw s::Error("Missing bitmap mask action binding");
      }
      note(
          mask.group->source,
          SpsmDiagnosticKind::Assumption,
          "bitmap-mask-empty-group-dominated",
          "Original empty group (source blend25/allbits/opacity1) retained, not treated as a generic no-op: under explicitly chosen native ordered semantics only, the later full Normal Fill opacity/coverage1 dominates prior scalar state. Source initial background0 and ancestor masks remain unchanged");
      note(
          mask.source->source,
          SpsmDiagnosticKind::Assumption,
          "bitmap-mask-restored",
          "Explicit native mask chain: raw R/ignored A, centered UV/HQ repeat, ordered full Normal Fill then scalar MaskAuxiliary0 Levels; native SFLOAT32 prefixes retain signed/HDR values and final layer coverage clamps[0,1]. Same immutable resource owner as content; source enum meanings/appearance unverified");
    }
    if (rough)
      note(
          rough->source,
          SpsmDiagnosticKind::Assumption,
          "bitmap-roughness-restored",
          "Independent Roughness restored from raw R; data A ignored. Prior omissions remain audit; Color is separate explicit opt-in and Normal stays disabled. Appearance unverified");
    if (color)
      note(
          color->source,
          SpsmDiagnosticKind::Assumption,
          "bitmap-color-restored",
          "Explicit RGB/ignored-A interpretation: immutable raw RGB -> native source-linear RGB Levels -> chosen named linear source-to-ACEScg matrix -> canonical white tint. No source ordinal inference, mask unwrapping or source appearance equivalence; original ancestry/coverage and independent Roughness Levels retained");
    note(a.source,
         SpsmDiagnosticKind::Assumption,
         "bitmap-uv-policy",
         "Caller chose projection-0 centered repeat factors and native "
         "repeat/filter; source wrap/filter ordinals are not interpreted. UV "
         "culling assumed inactive; scale multiplies native UV about 0.5");
    note(l.source,
         SpsmDiagnosticKind::Assumption,
         "bitmap-blend-policy",
         "Selected channels use explicit named native blend mapping and source opacity; "
         "missing sparse blend rows assumed Normal/1. Source enabled state and "
         "ancestor masks/visibility retained");
  }
  PreparedSpsm run()
  {
    for (std::size_t i = 0; i < ir.layers.size(); ++i) {
      const auto admitted = used, diagnostics = result.diagnostics.size();
      const auto &l = ir.layers[i];
      if (l.group)
        continue;
      try {
        const auto *color = use.color ? bitmap_source(l, 1) : nullptr;
        const auto *rough = use.roughness ? bitmap_source(l, 128) : nullptr;
        if (color || rough)
          candidate(i, color, rough);
      }
      catch (const MaskPolicyRequired &required) {
        used = resource_sum(admitted,
                            resource_product(result.diagnostics.size() - diagnostics,
                                             sizeof(SpsmImportDiagnostic) * 2));
        note(
            required.source,
            SpsmDiagnosticKind::Omission,
            "bitmap-mask-policy-required",
            "Active bitmap mask actions retain the entire content+mask branch disabled without hydration: separate native-ordered-fill-levels-raw-r-ignore-a mask policy required");
      }
      catch (const LevelsPolicyRequired &required) {
        used = resource_sum(admitted,
                            resource_product(result.diagnostics.size() - diagnostics,
                                             sizeof(SpsmImportDiagnostic) * 2));
        note(
            required.source,
            SpsmDiagnosticKind::Omission,
            "bitmap-levels-policy-required",
            "Bitmap Roughness Levels retained disabled without hydration: explicit NativeScalarLevels policy required");
      }
      catch (const Unsupported &) {
        used = resource_sum(admitted,
                            resource_product(result.diagnostics.size() - diagnostics,
                                             sizeof(SpsmImportDiagnostic) * 2));
        note(l.source,
             SpsmDiagnosticKind::Omission,
             use.color ? "bitmap-selected-unsupported" : "bitmap-roughness-unsupported",
             "Selected bitmap channels not restored: "
             "content/mask/coverage/transform/blend/resource is outside "
             "independent v2 single-Fill subset; complete source and prior "
             "omissions retained");
      }
    }
    return std::move(result);
  }
};
}  // namespace
PreparedSpsm enhance_prepared_spsm_bitmaps(const PreparedSpsm &p,
                                           const SpsmBitmapLoweringPolicy &policy,
                                           const SpsmBitmapResolver &resolver,
                                           const SpsmBitmapUse &use)
{
  return Lower(p, policy, resolver, use).run();
}
}  // namespace bts
