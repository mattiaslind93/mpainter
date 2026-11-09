// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmLevelsLowering.hh"
#include "ResourceAvailability.hh"
#include <set>

namespace bts { namespace {
namespace s = spsm;
namespace sem = spsm::semantic;
using Unsupported = SpsmLevelsUnsupported;
struct Lower {
  const PreparedSpsm &original;
  SpsmLevelsOptions options;
  std::size_t pi;
  const sem::PresetInspection &ir;
  const s::Preset &p;
  PreparedSpsm result;
  std::uint64_t used = 0;
  Lower(const PreparedSpsm &o, SpsmLevelsOptions v)
      : original(o), options(v), pi(primary(o)), ir(o.source->presets.at(pi)),
        p(o.source->source().presets.at(pi)) {
    add(resource_product(o.material.set.layers.size(), sizeof(TextureLayer) * 2));
    add(resource_product(o.layers.size(), sizeof(SpsmLayerBinding) * 2));
    add(resource_product(o.actions.size(), sizeof(SpsmActionBinding) * 2));
    add(resource_product(o.diagnostics.size(), sizeof(SpsmImportDiagnostic) * 2));
    for (const auto &l : o.material.set.layers) {
      add(resource_product(l.name.size(), 2));
      if (!l.effects.empty() || (l.mask && !l.mask->effects.empty()) ||
          !l.pixels.tiles().empty() || l.strokes || l.instance || l.quick_overlay)
        throw s::Error("Levels preparation requires an untouched detached source preparation");
    }
    result = o;
  }
  static std::size_t primary(const PreparedSpsm &o) {
    if (!o.source || !o.source->primary_preset ||
        *o.source->primary_preset >= o.source->presets.size() ||
        *o.source->primary_preset >= o.source->source().presets.size())
      throw s::Error("Levels preparation requires retained primary source IR");
    return *o.source->primary_preset;
  }
  void add(std::uint64_t n) {
    used = resource_sum(used, n);

  }
  const s::Node &node(s::NodeId id) const { return p.nodes.at(id); }
  const s::Object &object(s::NodeId id) const {
    const auto *o = std::get_if<s::Object>(&node(id).value);
    if (!o) throw Unsupported{id};
    return *o;
  }
  std::optional<s::NodeId> field(s::NodeId id, std::string_view key) const {
    std::optional<s::NodeId> found;
    for (const auto &f : object(id).fields) if (f.name == key) {
      if (found) throw s::Error("Duplicate SPSM Levels source field");
      found = f.value;
    }
    return found;
  }
  s::NodeId required(s::NodeId id, std::string_view key) const {
    const auto f = field(id, key);
    if (!f) throw Unsupported{id};
    return *f;
  }
  void fields(s::NodeId id, std::initializer_list<std::string_view> allowed) const {
    const auto &all = object(id).fields;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const auto &f = all[i];
      for (std::size_t j = 0; j < i; ++j)
        if (all[j].name == f.name) throw s::Error("Duplicate SPSM Levels source field");
      if (std::find(allowed.begin(), allowed.end(), f.name) == allowed.end())
        throw Unsupported{f.value};
    }
  }
  std::uint64_t bits(s::NodeId id) const {
    const auto &v = node(id).value;
    if (auto n = std::get_if<std::uint64_t>(&v)) return *n;
    if (auto n = std::get_if<std::int64_t>(&v); n && *n >= 0) return *n;
    if (auto n = std::get_if<s::IntValues>(&v); n && n->count == 1 && n->values[0] >= 0)
      return n->values[0];
    throw Unsupported{id};
  }
  double number(s::NodeId id) const {
    const auto &v = node(id).value;
    if (auto n = std::get_if<s::FloatValues>(&v); n && n->count == 1) {
      if (!std::isfinite(n->values[0])) throw Unsupported{id};
      return n->values[0];
    }
    return double(bits(id));
  }
  bool boolean(std::optional<s::NodeId> id, bool fallback = true) const {
    if (!id) return fallback;
    if (auto b = std::get_if<bool>(&node(*id).value)) return *b;
    throw Unsupported{*id};
  }
  float unit(std::optional<s::NodeId> id, float fallback = 1) const {
    if (!id) return fallback;
    const double x = number(*id);
    if (x < 0 || x > 1) throw Unsupported{*id};
    return float(x);
  }
  float scalar(s::NodeId id) const {
    auto v = std::get_if<s::FloatValues>(&node(id).value);
    if (!v || v->count != 3 || !std::isfinite(v->values[0]) ||
        v->values[0] != v->values[1] || v->values[1] != v->values[2])
      throw Unsupported{id};
    return v->values[0];
  }
  void selection(s::NodeId id) const {
    if (auto f = field(id, "selection")) {
      auto b = std::get_if<s::Blob>(&node(*f).value);
      if (!b || b->size) throw Unsupported{*f};
    }
    if (auto f = field(id, "tags"); f && bits(*f) != 0) throw Unsupported{*f};
  }
  void note(s::NodeId id, SpsmDiagnosticKind kind, std::string_view code, std::string_view detail) {
    add(sizeof(SpsmImportDiagnostic) * 2);
    result.diagnostics.push_back({pi, id, kind, code, detail});
    result.partial = true;
    result.appearance_verified = false;
  }
  SpsmLayerBinding &binding(s::NodeId id) {
    SpsmLayerBinding *found = nullptr;
    for (auto &b : result.layers) if (b.source == id) {
      if (found) throw s::Error("Duplicate Levels layer binding");
      found = &b;
    }
    if (!found) throw s::Error("Missing Levels layer binding");
    return *found;
  }
  TextureLayer &native(const QUuid &id) {
    TextureLayer *found = nullptr;
    for (auto &l : result.material.set.layers) if (l.id == id) {
      if (found) throw s::Error("Duplicate Levels native layer");
      found = &l;
    }
    if (!found) throw s::Error("Missing Levels native layer");
    return *found;
  }
  std::pair<float, LayerBlend> blending(const std::vector<sem::Blending> &rows) const {
    float opacity = 1;
    LayerBlend mode = LayerBlend::Normal;
    bool found = false;
    for (const auto &b : rows) {
      fields(b.source, {"channelTypes", "blendingMode", "opacity"});
      if (!b.channel_types || !b.source_mode) throw Unsupported{b.source};
      const auto channels = bits(*b.channel_types);
      if (!channels) throw Unsupported{b.source};
      if (!(channels & 128)) continue;
      if (found) throw Unsupported{b.source};
      found = true;
      switch (bits(*b.source_mode)) {
        case 1: mode = LayerBlend::Replace; break;
        case 2: mode = LayerBlend::Normal; break;
        default: throw Unsupported{b.source};
      }
      opacity = unit(b.opacity);
    }
    return {opacity, mode};
  }
  void coverage(const sem::Layer &l) const {
    fields(l.source, {"GUIcollapsedState", "colorTag", "enabled", "enabledUVTileDefault",
      "enabledUVTileList", "label", "maskActions", "maskEnabled", "maskInitial",
      "perChannelBlending", "subStack", "uid", "enabledUVTileMask", "gammaCompensation",
      "actions", "enabledGeometryMask", "enabledMeshDefault", "enabledMeshList",
      "explicitPersistentCachePin", "geometryMaskType"});
    if (!boolean(l.enabled) || boolean(field(l.source, "gammaCompensation"), false))
      throw Unsupported{l.source};
    for (const auto key : {"enabledMeshDefault", "enabledUVTileDefault", "enabledUVTileMask"})
      if (!boolean(field(l.source, key))) throw Unsupported{l.source};
    boolean(field(l.source, "enabledGeometryMask"));
    for (const auto key : {"enabledMeshList", "enabledUVTileList"}) if (auto id = field(l.source, key)) {
      auto a = std::get_if<s::Array>(&node(*id).value);
      if (!a || !a->elements.empty()) throw Unsupported{*id};
    }
    if (auto id = field(l.source, "geometryMaskType"); id && bits(*id) != 0)
      throw Unsupported{*id};
    unit(l.mask_initial);
    if (boolean(l.mask_enabled)) for (auto ai : l.mask_actions)
      if (boolean(ir.actions.at(ai).enabled)) throw Unsupported{ir.actions.at(ai).source};
    blending(l.blending);
  }
  s::NodeId levels_object(const sem::Action &a) const {
    return validate_spsm_levels_action(p,a);
  }
  LevelsEffect curve(const sem::Action &a) const {
    return parse_spsm_scalar_levels(p,a,options.policy);
  }
  struct Initial {float value, opacity;};
  Initial initial(const sem::Action &a) const {
    fields(a.source, {"backfaceCulling", "backfaceCullingAngle", "backfaceCullingHardness", "channelTypes",
      "dddEulerAngles", "dddPosition", "dddSize", "dddWrap", "depthCulling", "depthCullingHardness",
      "enabled", "filtering", "label", "perChannelBlending", "projection", "selection", "sideHardness",
      "sources", "tags", "uid", "uvSamplingWrap", "uvTransformation0", "uvTransformation1",
      "cylinderCullingAngle", "cylinderHideCaps", "dddGrid", "uvGrid", "uvTransferChannel", "uvTransformation"});
    selection(a.source);
    if (!a.children.empty()) throw Unsupported{a.source};
    for (auto key : {"backfaceCulling", "depthCulling", "cylinderHideCaps"})
      if (boolean(field(a.source, key), false)) throw Unsupported{a.source};
    if (!(bits(required(a.source, "channelTypes")) & 128)) throw Unsupported{a.source};
    const auto [opacity, mode] = blending(a.blending);
    (void)mode;
    const sem::Source *source = nullptr;
    for (auto id : a.sources) {
      const auto channels = bits(required(id, "channelTypes"));
      if (!channels) throw Unsupported{id};
      if (!(channels & 128)) continue;
      if (source) throw Unsupported{id};
      for (const auto &binding_source : ir.sources) if (binding_source.source == id) {
        if (source) throw s::Error("Duplicate Levels source binding");
        source = &binding_source;
      }
      if (!source) throw Unsupported{id};
    }
    if (!source || source->kind != sem::Source::Kind::Uniform) throw Unsupported{a.source};
    fields(source->source, {"channelTypes", "color", "opacity", "uid"});
    if (!source->color) throw Unsupported{source->source};
    const auto value = scalar(*source->color);
    if (value < 0 || value > 1) throw Unsupported{source->source};
    return {value, opacity * unit(source->opacity)};
  }
  bool candidate(const sem::Layer &l) const {
    if (l.group) return false;
    for (auto ai : l.actions) {
      const auto &a = ir.actions.at(ai);
      if (a.kind != sem::ActionKind::Levels || !boolean(a.enabled)) continue;
      if (auto c = field(a.source, "channelTypes"); c && (bits(*c) & 128)) return true;
    }
    return false;
  }
  void restore(std::size_t li) {
    const auto &l = ir.layers.at(li);
    auto &b = binding(l.source);
    auto &out = native(b.native_layer);
    if (!b.disabled || out.kind != LayerKind::Fill ||
        fill_has_texture(out.fill) || fill_has_anchors(out.fill) || out.fill.native_material || out.fill.atom)
      throw Unsupported{l.source};
    const auto expected_parent = l.parent ? binding(ir.layers.at(*l.parent).source).native_layer : QUuid{};
    if (out.parent != expected_parent)
      throw s::Error("Levels preparation native hierarchy differs from retained source");
    coverage(l);
    std::set<std::size_t> parents;
    auto parent = l.parent;
    while (parent) {
      add(sizeof(std::size_t) + sizeof(void *) * 4);
      if (*parent == li || !parents.insert(*parent).second) throw s::Error("Cyclic Levels ancestry");
      const auto &ancestor = ir.layers.at(*parent);
      auto &pb = binding(ancestor.source);
      const auto &parent_layer = native(pb.native_layer);
      if (!ancestor.group || pb.disabled || !parent_layer.visible ||
          !parent_layer.channels[2]) throw Unsupported{ancestor.source};
      const auto grandparent = ancestor.parent ? binding(ir.layers.at(*ancestor.parent).source).native_layer : QUuid{};
      if (parent_layer.parent != grandparent)
        throw s::Error("Levels ancestor hierarchy differs from retained source");
      coverage(ancestor);
      parent = ancestor.parent;
    }
    const sem::Action *fill = nullptr;
    std::vector<LevelsEffect> effects;
    std::vector<s::NodeId> lowered;
    add(resource_product(l.actions.size(), sizeof(LevelsEffect) * 2 + sizeof(s::NodeId) * 2));
    for (auto ai : l.actions) {
      const auto &a = ir.actions.at(ai);
      if (!boolean(a.enabled)) continue;
      if (a.kind == sem::ActionKind::Fill) {
        // One initial content source only. A second Fill is never flattened.
        if (fill || !effects.empty()) throw Unsupported{a.source};
        fill = &a;
        initial(a);
        lowered.push_back(a.source);
      } else if (a.kind == sem::ActionKind::Levels) {
        levels_object(a);
        const auto channels = bits(required(a.source, "channelTypes"));
        if (!channels) throw Unsupported{a.source};
        if (!(channels & 128)) continue; // Retained, disjoint channel; never evaluated as color.
        if (!fill || channels != 128) throw Unsupported{a.source};
        effects.push_back(curve(a));
        lowered.push_back(a.source);
      } else throw Unsupported{a.source};
    }
    if (!fill || effects.empty()) throw Unsupported{l.source};
    const auto value = initial(*fill);
    const auto [opacity, mode] = blending(l.blending);
    // No changes precede complete source/ancestor/dependency validation.
    // Base compiler disabled this entire unsupported stack, so its untouched
    // defaults in other channels must remain disabled when restoring Roughness.
    out.channels.fill(false);
    out.channels[2] = true;
    out.ao_enabled = false;
    out.values.roughness = value.value;
    out.opacity[2] = opacity * value.opacity;
    out.blending[2] = mode;
    out.effects = std::move(effects);
    out.mask = LayerMask{};
    out.mask->enabled = boolean(l.mask_enabled);
    out.mask->background = unit(l.mask_initial);
    out.visible = true;
    b.disabled = false;
    for (auto id : lowered) {
      for (auto &a : result.actions) if (a.source == id && a.native_layer == out.id) a.lowered = true;
      note(id, SpsmDiagnosticKind::Assumption, "native-scalar-levels-policy",
        "Explicit NativeScalarLevels: equal RGB interpreted as scalar data; reciprocal gamma, signed native intermediates, clamp 0 off/1 final [0,1]; source identity remains unverified");
    }
    note(l.source, SpsmDiagnosticKind::Assumption, "levels-roughness-restored",
      "Only independently validated uniform Roughness Fill followed by ordered Levels restored; original omissions retained, other channels disabled; named Normal/Replace and bottom-to-top order remain explicit source policies");
  }
  PreparedSpsm run() {
    for (std::size_t li = 0; li < ir.layers.size(); ++li) {
      const auto before = used, diagnostics = result.diagnostics.size();
      try {
        const auto &l = ir.layers[li];
        if (!candidate(l)) continue;
        if (options.policy != SpsmLevelsPolicy::NativeScalarLevels) {
          note(l.source, SpsmDiagnosticKind::Omission, "levels-policy-required",
            "Roughness Levels retained disabled: explicit NativeScalarLevels policy required");
          continue;
        }
        restore(li);
        // Ancestor sets and validation scratch have now been released. Keep
        // only the newly retained effects/diagnostics in the admission total.
        used = before;
        add(resource_product(native(binding(l.source).native_layer).effects.size(), sizeof(LevelsEffect) * 2));
        add(resource_product(result.diagnostics.size() - diagnostics, sizeof(SpsmImportDiagnostic) * 2));
      } catch (Unsupported u) {
        used = before;
        note(u.source, SpsmDiagnosticKind::Omission, "levels-unsupported-dependency",
          "Roughness Levels chain remains disabled: unknown source fields, channel/scalar contract, sequence, coverage, blend, native domain or nonuniform dependency");
      }
    }
    return std::move(result);
  }
};
} // namespace
PreparedSpsm prepare_spsm_levels(const PreparedSpsm &original, SpsmLevelsOptions options) {
  return Lower(original, options).run();
}
} // namespace bts
