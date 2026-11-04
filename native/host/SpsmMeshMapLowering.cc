#include "SpsmMeshMapLowering.hh"
#include "ResourceBudget.hh"
#include <set>
namespace bts {
namespace {
namespace s = spsm;
namespace sem = spsm::semantic;
struct Unsupported {
  s::NodeId source;
};
struct Lower {
  const PreparedSpsm &original;
  SpsmMeshMapOptions options;
  std::size_t pi;
  const sem::PresetInspection &ir;
  const s::Preset &p;
  PreparedSpsm result;
  std::uint64_t used = 0, limit;
  static std::size_t primary(const PreparedSpsm &o) {
    if (!o.source || !o.source->primary_preset ||
        *o.source->primary_preset >= o.source->presets.size() ||
        *o.source->primary_preset >= o.source->source().presets.size())
      throw s::Error(
          "Mesh-map preparation requires retained primary source IR");
    return *o.source->primary_preset;
  }
  Lower(const PreparedSpsm &o, SpsmMeshMapOptions v)
      : original(o), options(v), pi(primary(o)), ir(o.source->presets.at(pi)),
        p(o.source->source().presets.at(pi)),
        limit(v.memory_budget_bytes
                  ? std::min(v.memory_budget_bytes, ResourceBudget{}.ram())
                  : ResourceBudget{}.ram()) {
    add(resource_product(o.material.set.layers.size(),
                         sizeof(TextureLayer) * 2));
    add(resource_product(o.layers.size(), sizeof(SpsmLayerBinding) * 2));
    add(resource_product(o.actions.size(), sizeof(SpsmActionBinding) * 2));
    add(resource_product(o.diagnostics.size(),
                         sizeof(SpsmImportDiagnostic) * 2));
    for (const auto &l : o.material.set.layers) {
      add(resource_product(l.name.size(), 2));
      add(resource_product(l.effects.size(), sizeof(LevelsEffect) * 2));
      if (l.mask)
        add(resource_product(l.mask->effects.size(), sizeof(MaskEffect) * 2));
    }
    result = o; // Pixels, source inspection and immutable resource owners are
                // shared.
  }
  void add(std::uint64_t n) {
    used = resource_sum(used, n);
    if (used > limit)
      throw s::Error(
          "SPSM mesh-map preparation needs RAM " + resource_mebibytes(used) +
          " (" + std::to_string(used) + " bytes); available worker budget " +
          resource_mebibytes(limit) + " (" + std::to_string(limit) +
          " bytes), minimum of explicit policy and current RAM budget");
  }
  const s::Node &node(s::NodeId id) const { return p.nodes.at(id); }
  const s::Object &object(s::NodeId id) const {
    auto *v = std::get_if<s::Object>(&node(id).value);
    if (!v)
      throw Unsupported{id};
    return *v;
  }
  std::optional<s::NodeId> field(s::NodeId id, std::string_view key) const {
    std::optional<s::NodeId> found;
    for (const auto &f : object(id).fields)
      if (f.name == key) {
        if (found)
          throw s::Error("Duplicate mesh-map source field");
        found = f.value;
      }
    return found;
  }
  s::NodeId required(s::NodeId id, std::string_view key) const {
    auto f = field(id, key);
    if (!f)
      throw Unsupported{id};
    return *f;
  }
  void fields(s::NodeId id,
              std::initializer_list<std::string_view> allowed) const {
    const auto &all = object(id).fields;
    for (std::size_t i = 0; i < all.size(); ++i) {
      for (std::size_t j = 0; j < i; ++j)
        if (all[j].name == all[i].name)
          throw s::Error("Duplicate mesh-map source field");
      if (std::find(allowed.begin(), allowed.end(), all[i].name) ==
          allowed.end())
        throw Unsupported{all[i].value};
    }
  }
  std::uint64_t bits(s::NodeId id) const {
    const auto &v = node(id).value;
    if (auto n = std::get_if<std::uint64_t>(&v))
      return *n;
    if (auto n = std::get_if<std::int64_t>(&v); n && *n >= 0)
      return *n;
    if (auto n = std::get_if<s::IntValues>(&v);
        n && n->count == 1 && n->values[0] >= 0)
      return n->values[0];
    throw Unsupported{id};
  }
  double number(s::NodeId id) const {
    if (auto n = std::get_if<s::FloatValues>(&node(id).value);
        n && n->count == 1) {
      if (!std::isfinite(n->values[0]))
        throw Unsupported{id};
      return n->values[0];
    }
    return double(bits(id));
  }
  bool boolean(std::optional<s::NodeId> id, bool fallback = true) const {
    if (!id)
      return fallback;
    if (auto n = std::get_if<bool>(&node(*id).value))
      return *n;
    throw Unsupported{*id};
  }
  float unit(std::optional<s::NodeId> id, float fallback = 1) const {
    if (!id)
      return fallback;
    auto n = number(*id);
    if (n < 0 || n > 1)
      throw Unsupported{*id};
    return float(n);
  }
  bool text(s::NodeId id, std::string_view expected) const {
    auto n = std::get_if<std::string>(&node(id).value);
    return n && *n == expected;
  }
  void pair(s::NodeId id, double x, double y) const {
    auto n = std::get_if<s::FloatValues>(&node(id).value);
    if (!n || n->count != 2 || n->values[0] != x || n->values[1] != y)
      throw Unsupported{id};
  }
  void note(s::NodeId id, SpsmDiagnosticKind kind, std::string_view code,
            std::string_view detail) {
    add(sizeof(SpsmImportDiagnostic) * 2);
    result.diagnostics.push_back({pi, id, kind, code, detail});
    result.partial = true;
    result.appearance_verified = false;
  }
  SpsmLayerBinding &binding(s::NodeId id) {
    SpsmLayerBinding *found = nullptr;
    for (auto &b : result.layers)
      if (b.source == id) {
        if (found)
          throw s::Error("Duplicate mesh-map layer binding");
        found = &b;
      }
    if (!found)
      throw s::Error("Missing mesh-map layer binding");
    return *found;
  }
  TextureLayer &native(const QUuid &id) {
    TextureLayer *found = nullptr;
    for (auto &l : result.material.set.layers)
      if (l.id == id) {
        if (found)
          throw s::Error("Duplicate mesh-map native layer");
        found = &l;
      }
    if (!found)
      throw s::Error("Missing mesh-map native layer");
    return *found;
  }
  const sem::Source &source(s::NodeId id) const {
    const sem::Source *found = nullptr;
    for (const auto &v : ir.sources)
      if (v.source == id) {
        if (found)
          throw s::Error("Duplicate mesh-map semantic source");
        found = &v;
      }
    if (!found)
      throw s::Error("Missing mesh-map semantic source");
    return *found;
  }
  const sem::Source *candidate(const sem::Layer &l) const {
    if (l.group)
      return nullptr;
    // Only direct action sources. Never recurse into procedural input sources.
    for (auto ai : l.actions) {
      const auto &action = ir.actions.at(ai);
      if (action.kind != sem::ActionKind::Fill || !boolean(action.enabled))
        continue;
      for (auto id : action.sources) {
        const auto &v = source(id);
        if (v.kind != sem::Source::Kind::Bitmap || !v.bitmap)
          continue;
        auto url = field(*v.bitmap, "urlToBitmapRes");
        if (url && text(*url, "resourcealias:AmbientOcclusion") &&
            (bits(required(id, "channelTypes")) & 1))
          return &v;
      }
    }
    return nullptr;
  }
  std::pair<float, LayerBlend> blend(const std::vector<sem::Blending> &rows,
                                     bool initial = false) const {
    bool found = false;
    float opacity = 1;
    auto mode = LayerBlend::Normal;
    for (const auto &r : rows) {
      fields(r.source, {"channelTypes", "blendingMode", "opacity"});
      if (!(bits(required(r.source, "channelTypes")) & 1))
        continue;
      if (found)
        throw Unsupported{r.source};
      found = true;
      switch (bits(required(r.source, "blendingMode"))) {
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
      default:
        throw Unsupported{r.source};
      }
      if (initial && mode != LayerBlend::Normal && mode != LayerBlend::Replace)
        throw Unsupported{r.source};
      opacity = unit(r.opacity);
    }
    return {opacity, mode};
  }
  void coverage(const sem::Layer &l) const {
    fields(l.source, {"GUIcollapsedState",
                      "colorTag",
                      "enabled",
                      "enabledUVTileDefault",
                      "enabledUVTileList",
                      "enabledUVTileMask",
                      "gammaCompensation",
                      "label",
                      "maskActions",
                      "maskEnabled",
                      "maskInitial",
                      "perChannelBlending",
                      "subStack",
                      "uid",
                      "actions",
                      "enabledGeometryMask",
                      "enabledMeshDefault",
                      "enabledMeshList",
                      "explicitPersistentCachePin",
                      "geometryMaskType"});
    boolean(l.enabled);
    if (boolean(field(l.source, "gammaCompensation"), false))
      throw Unsupported{l.source};
    for (auto key :
         {"enabledMeshDefault", "enabledUVTileDefault", "enabledUVTileMask"})
      if (!boolean(field(l.source, key)))
        throw Unsupported{l.source};
    boolean(field(l.source, "enabledGeometryMask"));
    for (auto key : {"enabledMeshList", "enabledUVTileList"})
      if (auto f = field(l.source, key)) {
        auto a = std::get_if<s::Array>(&node(*f).value);
        if (!a || !a->elements.empty())
          throw Unsupported{*f};
      }
    if (auto f = field(l.source, "geometryMaskType"); f && bits(*f) != 0)
      throw Unsupported{*f};
    unit(l.mask_initial);
    if (boolean(l.mask_enabled))
      for (auto ai : l.mask_actions)
        if (boolean(ir.actions.at(ai).enabled))
          throw Unsupported{ir.actions.at(ai).source};
    blend(l.blending);
  }
  void mapping(const sem::Action &a) const {
    fields(a.source, {"backfaceCulling",
                      "backfaceCullingAngle",
                      "backfaceCullingHardness",
                      "channelTypes",
                      "dddEulerAngles",
                      "dddPosition",
                      "dddSize",
                      "dddWrap",
                      "depthCulling",
                      "depthCullingHardness",
                      "enabled",
                      "filtering",
                      "label",
                      "perChannelBlending",
                      "projection",
                      "selection",
                      "sideHardness",
                      "sources",
                      "tags",
                      "uid",
                      "uvSamplingWrap",
                      "uvTransformation0",
                      "uvTransformation1"});
    if (object(a.source).class_name != "DataActionFill" ||
        bits(required(a.source, "projection")) != 0)
      throw Unsupported{a.source};
    for (auto key : {"backfaceCulling", "depthCulling"})
      if (boolean(field(a.source, key), false))
        throw Unsupported{a.source};
    if (auto f = field(a.source, "selection")) {
      auto b = std::get_if<s::Blob>(&node(*f).value);
      if (!b || b->size)
        throw Unsupported{*f};
    }
    if (auto f = field(a.source, "tags"); f && bits(*f) != 0)
      throw Unsupported{*f};
    // Deliberately gate the observed legacy UV record. Neither wrap/filter nor
    // source alpha/color enums are converted to native ordinals.
    for (auto key : {"uvTransformation0", "uvTransformation1"}) {
      const auto id = required(a.source, key);
      fields(id, {"GUIPivot", "GUIScaleRatioLocked", "rotation", "scale",
                  "translation"});
      if (object(id).class_name != "Data2DTransformation" ||
          number(required(id, "rotation")) != 0)
        throw Unsupported{id};
      boolean(field(id, "GUIScaleRatioLocked"));
      pair(required(id, "scale"), 1, 1);
      pair(required(id, "translation"), 0, 0);
      pair(required(id, "GUIPivot"), .5, .5);
    }
    bits(required(a.source, "filtering"));
    bits(required(a.source, "uvSamplingWrap"));
  }
  void restore(std::size_t li, const sem::Source &v) {
    const auto &l = ir.layers.at(li);
    auto &b = binding(l.source);
    auto &out = native(b.native_layer);
    if (!b.disabled || out.kind != LayerKind::Fill ||
        !out.effects.empty() || (out.mask && !out.mask->effects.empty()) ||
        !out.pixels.tiles().empty() || out.strokes || out.instance ||
        out.quick_overlay || fill_has_texture(out.fill) ||
        fill_has_anchors(out.fill) || out.fill.native_material || out.fill.atom)
      throw Unsupported{l.source};
    coverage(l);
    auto expected_parent =
        l.parent ? binding(ir.layers.at(*l.parent).source).native_layer
                 : QUuid{};
    if (out.parent != expected_parent)
      throw s::Error(
          "Mesh-map preparation hierarchy differs from retained source");
    std::set<std::size_t> visited;
    auto parent = l.parent;
    while (parent) {
      add(sizeof(std::size_t) + sizeof(void *) * 4);
      if (*parent == li || !visited.insert(*parent).second)
        throw s::Error("Cyclic mesh-map ancestry");
      const auto &ancestor = ir.layers.at(*parent);
      auto &pb = binding(ancestor.source);
      const auto &n = native(pb.native_layer);
      if (!ancestor.group || pb.disabled || !n.channels[0] ||
          !n.effects.empty() || (n.mask && !n.mask->effects.empty()))
        throw Unsupported{ancestor.source};
      coverage(ancestor);
      const auto [op, mode] = blend(ancestor.blending);
      if (n.visible != boolean(ancestor.enabled) || n.opacity[0] != op ||
          n.blending[0] != mode || !n.mask ||
          n.mask->enabled != boolean(ancestor.mask_enabled) ||
          n.mask->background != unit(ancestor.mask_initial))
        throw s::Error(
            "Mesh-map ancestor metadata differs from retained source");
      const auto gp =
          ancestor.parent
              ? binding(ir.layers.at(*ancestor.parent).source).native_layer
              : QUuid{};
      if (n.parent != gp)
        throw s::Error(
            "Mesh-map ancestor hierarchy differs from retained source");
      parent = ancestor.parent;
    }
    const sem::Action *fill = nullptr;
    for (auto ai : l.actions) {
      const auto &a = ir.actions.at(ai);
      if (!boolean(a.enabled))
        continue;
      if (fill || a.kind != sem::ActionKind::Fill || !a.children.empty())
        throw Unsupported{a.source};
      fill = &a;
    }
    if (!fill)
      throw Unsupported{l.source};
    mapping(*fill);
    if (!(bits(required(fill->source, "channelTypes")) & 1))
      throw Unsupported{fill->source};
    unsigned count = 0;
    for (auto id : fill->sources) {
      const auto &src = source(id);
      const auto channels = bits(required(id, "channelTypes"));
      if (channels & 1) {
        ++count;
        if (id != v.source || channels != 1)
          throw Unsupported{id};
      } else if (src.kind != sem::Source::Kind::Uniform)
        throw Unsupported{id};
    }
    if (count != 1)
      throw Unsupported{fill->source};
    fields(v.source, {"bitmap", "channelTypes", "opacity", "uid"});
    fields(*v.bitmap, {"alphaType", "colorSpace", "uid", "urlToBitmapRes"});
    if (object(*v.bitmap).class_name != "DataBitmap" ||
        !text(required(*v.bitmap, "urlToBitmapRes"),
              "resourcealias:AmbientOcclusion"))
      throw Unsupported{v.source};
    // Explicit policy overrides these source ordinal contracts, not other
    // unknown fields/dependencies. Preserve numeric source values in the IR.
    bits(required(*v.bitmap, "alphaType"));
    bits(required(*v.bitmap, "colorSpace"));
    const auto [op, mode] = blend(l.blending);
    const auto [action_op, action_mode] = blend(fill->blending, true);
    (void)action_mode;
    if (mode == LayerBlend::Disabled)
      throw Unsupported{l.source};
    const auto source_op = unit(v.opacity);
    SpsmActionBinding *ab = nullptr;
    for (auto &a : result.actions)
      if (a.source == fill->source) {
        if (ab || a.native_layer != out.id)
          throw s::Error("Invalid mesh-map action binding");
        ab = &a;
      }
    if (!ab)
      throw s::Error("Missing mesh-map action binding");
    add(sizeof(LevelsEffect) * 2);
    LevelsEffect effect;
    effect.kind = ContentEffectKind::Generator;
    effect.channel = 0;
    effect.generator = 3;
    effect.map_filter = 0;
    effect.contrast = 1;
    effect.bias = 0;
    effect.invert = false;
    effect.curve = 0;
    effect.opacity = 1;
    effect.blending = LayerBlend::Normal;
    effect.name = "AO alias (explicit native scalar policy)";
    std::vector<LevelsEffect> effects;
    effects.push_back(std::move(effect));
    // All source validation and allocations precede changes to this candidate.
    out.fill = FillProjection{};
    out.values.base_color = {1, 1, 1, 1};
    out.channels.fill(false);
    out.channels[0] = true;
    out.ao_enabled = false;
    out.opacity[0] = op * action_op * source_op;
    out.blending[0] = mode;
    out.effects = std::move(effects);
    out.mask = LayerMask{};
    out.mask->enabled = boolean(l.mask_enabled);
    out.mask->background = unit(l.mask_initial);
    out.visible = boolean(l.enabled);
    b.disabled = false;
    ab->lowered = true;
    note(v.source, SpsmDiagnosticKind::Assumption, "meshmap-ao-color-restored",
         "Only direct independent AmbientOcclusion alias restored into Base "
         "Color, not AO output. Live same-set/matching-tile native AO scalar R "
         "decoded from premultiplied map storage, clamped [0,1], replicated "
         "scene-linear RGB; map A is not an output coverage mask, uncovered "
         "texels yield 0. Explicit nearest/clamp and identity-UV destination "
         "policy; source color/alpha/filter/wrap ordinals remain "
         "uninterpreted. Appearance unverified; prior omissions and "
         "unsupported Gloss/Diffuse/Specular remain retained/off");
    note(fill->source, SpsmDiagnosticKind::Assumption,
         "meshmap-ao-color-contract",
         "Single Fill action Normal/Replace, layer named blend/opacity, source "
         "opacity, original enabled state and ancestor masks/visibility "
         "retained under existing bottom-to-top/native mask policy. AO map "
         "required on every destination tile before atomic preset apply; no "
         "procedural graph inputs are unwrapped");
  }
  PreparedSpsm run() {
    for (std::size_t li = 0; li < ir.layers.size(); ++li) {
      const auto before = used;
      try {
        auto v = candidate(ir.layers[li]);
        if (!v)
          continue;
        // Detect our prior restoration without treating compiler defaults in
        // disabled unsupported stacks as authored/enabled Color.
        const auto &output = native(binding(ir.layers[li].source).native_layer);
        const bool restored =
            std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                        [&](const auto &d) {
                          return d.preset == pi && d.source == v->source &&
                                 d.code == "meshmap-ao-color-restored";
                        });
        if (restored || (output.visible && output.channels[0]))
          throw s::Error("Mesh-map Color candidate is not a fresh disabled "
                         "source preparation");
        if (options.policy !=
            SpsmMeshMapPolicy::NativeAOGeneratorUVIdentityNearest) {
          note(v->source, SpsmDiagnosticKind::Omission,
               "meshmap-ao-color-policy-required",
               "Direct AO alias Color branch remains disabled: explicit native "
               "AO Generator, matching tile, identity UV and nearest/clamp "
               "policy required; no source ordinal interpretation or "
               "procedural substitution");
          continue;
        }
        restore(li, *v);
      } catch (Unsupported u) {
        used = before;
        note(u.source, SpsmDiagnosticKind::Omission,
             "meshmap-ao-color-unsupported-dependency",
             "AO alias Color remains disabled: unknown "
             "source/action/geometry/UV field, nonindependent source sequence, "
             "unsupported mask/ancestor/blend or changed preparation. Original "
             "IR and omissions retained");
      }
    }
    return std::move(result);
  }
};
} // namespace
PreparedSpsm enhance_prepared_spsm_mesh_maps(const PreparedSpsm &p,
                                             SpsmMeshMapOptions o) {
  return Lower(p, o).run();
}
} // namespace bts
