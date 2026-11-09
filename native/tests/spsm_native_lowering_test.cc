// SPDX-License-Identifier: GPL-3.0-or-later
#include "SmartMaskProbe.hh"
#include "SpsmMaterialImport.hh"
#include "SpsmPreview.hh"
#include "MaskPresetTextures.hh"
#include <QBuffer>
#include "SpsmNativeSemantics.hh"
#include "ResourceAvailability.hh"
#include "SpatialOracleScope.hh"
#include "GpuLayerEffects.hh"
#include <bit>
#include <iostream>
#include <set>
#include <QTemporaryDir>
using namespace bts;
namespace s = bts::spsm;
namespace sem = bts::spsm::semantic;
namespace {
void require(bool yes, const char *message)
{
  if (!yes)
    throw std::runtime_error(message);
}
struct Builder {
  s::Preset p;
  template<class T> s::NodeId add(T value)
  {
    auto n = p.nodes.size();
    p.nodes.push_back({18, 0, 1, std::move(value)});
    return n;
  }
  s::NodeId integer(std::uint64_t n)
  {
    return add(n);
  }
  s::NodeId scalar(float n)
  {
    s::FloatValues v;
    v.count = 1;
    v.values[0] = n;
    return add(v);
  }
  s::NodeId rgb(std::array<float, 3> n)
  {
    s::FloatValues v;
    v.count = 3;
    std::copy(n.begin(), n.end(), v.values.begin());
    return add(v);
  }
  s::NodeId boolean(bool b)
  {
    return add(b);
  }
  s::NodeId text(std::string v)
  {
    return add(std::move(v));
  }
  s::NodeId object(std::string name, std::initializer_list<s::Field> fields)
  {
    return add(s::Object{std::move(name), fields});
  }
  s::NodeId array(std::vector<s::NodeId> items)
  {
    return add(s::Array{std::move(items)});
  }
  s::NodeId stack(std::string name, std::vector<s::NodeId> items)
  {
    return object(std::move(name), {{"items", array(std::move(items))}});
  }
  s::NodeId blend(std::uint64_t bit, std::uint64_t mode = 2, float opacity = 1)
  {
    return object("DataBlending",
                  {{"channelTypes", integer(bit)},
                   {"blendingMode", integer(mode)},
                   {"opacity", scalar(opacity)}});
  }
  s::NodeId uniform(std::uint64_t bit, std::array<float, 3> color, float opacity = 1)
  {
    return object(
        "DataSourceUniform",
        {{"channelTypes", integer(bit)}, {"color", rgb(color)}, {"opacity", scalar(opacity)}});
  }
  s::NodeId fill(std::vector<s::NodeId> sources, std::vector<s::NodeId> blends = {})
  {
    return object("DataActionFill",
                  {{"channelTypes", integer(UINT64_MAX)},
                   {"enabled", boolean(true)},
                   {"sources", array(std::move(sources))},
                   {"perChannelBlending", array(std::move(blends))},
                   {"projection", integer(9001)}});
  }
  s::NodeId layer(std::string name,
                  std::vector<s::NodeId> actions,
                  std::vector<s::NodeId> masks = {},
                  float background = 1,
                  std::vector<s::NodeId> blends = {})
  {
    return object("DataLayerColor",
                  {{"label", text(std::move(name))},
                   {"enabled", boolean(true)},
                   {"actions", stack("DataStackActions", std::move(actions))},
                   {"maskActions", stack("DataStackActions", std::move(masks))},
                   {"maskEnabled", boolean(true)},
                   {"maskInitial", scalar(background)},
                   {"perChannelBlending", array(std::move(blends))}});
  }
  s::NodeId folder(std::string name, std::vector<s::NodeId> children)
  {
    return object("DataLayerGroup",
                  {{"label", text(std::move(name))},
                   {"subStack", stack("DataStackLayers", std::move(children))},
                   {"maskInitial", scalar(1)}});
  }
  s::Archive archive(s::NodeId root)
  {
    p.root = object("DataLayersPreset", {{"layerGroup", root}});
    p.dataset = "/preset.bin";
    p.encoded = {17, 42};
    s::Archive a;
    a.source = "synthetic.spsm";
    a.presets.push_back(std::move(p));
    return a;
  }
};
SpsmImportOptions color_policy()
{
  SpsmImportOptions o;
  o.uniform_rgb_to_scene_linear = [](std::array<float, 3> rgb) {
    return std::array<float, 3>{rgb[0] * 2, rgb[1] * 3, rgb[2] * 4};
  };
  return o;
}
bool has(const PreparedSpsm &p, std::string_view code)
{
  return std::any_of(
      p.diagnostics.begin(), p.diagnostics.end(), [&](const auto &d) { return d.code == code; });
}
const TextureLayer &named(const PreparedSpsm &p, const char *name)
{
  for (const auto &l : p.material.set.layers)
    if (l.name == name)
      return l;
  throw std::runtime_error("Missing native layer");
}
void hierarchy(const PreparedSpsm &p)
{
  std::set<QUuid> ids;
  std::map<QUuid, std::pair<std::size_t, int>> positions;
  const auto &layers = p.material.set.layers;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    require(ids.insert(layers[i].id).second, "native IDs alias");
    positions.emplace(layers[i].id, std::pair(i, layers[i].depth));
  }
  for (std::size_t i = 0; i < layers.size(); ++i)
    if (!layers[i].parent.isNull()) {
      const auto found = positions.at(layers[i].parent);
      require(found.first > i && found.second + 1 == layers[i].depth,
              "native postorder/depth broken");
    }
  require(layers.back().kind == LayerKind::Folder && layers.back().parent.isNull() &&
              layers.back().depth == 0,
          "native root is not Folder");
}
void synthetic()
{
  Builder b;
  auto bottom = b.layer("bottom",
                        {b.fill({b.uniform(1, {.8f, .1f, .2f}),
                                 b.uniform(2, {-.2f, -.2f, -.2f}),
                                 b.uniform(128, {.25f, .25f, .25f}, .5f)},
                                {b.blend(1), b.blend(2, 10), b.blend(128)})},
                        {},
                        1,
                        {b.blend(1), b.blend(2, 1), b.blend(128, 2, .8f)});
  auto top = b.layer("top", {b.fill({b.uniform(1, {.1f, .2f, .9f}, .5f)})}, {}, .4f);
  auto inner = b.folder("inner", {top});
  auto root = b.folder("root", {bottom, inner});
  auto input = sem::inspect_archive(b.archive(root));
  const auto original = input;
  auto prepared = prepare_spsm_material(std::move(input), color_policy());
  require(&original.source() == &prepared.source->source(),
          "retained source was copied or mutated");
  require(prepared.source->source().presets[0].encoded == std::vector<std::uint8_t>({17, 42}),
          "source bytes changed");
  hierarchy(prepared);
  require(prepared.material.set.layers.size() == 4, "native layer count mismatch");
  const auto &layers = prepared.material.set.layers;
  require(layers[0].name == "bottom" && layers[1].name == "top" && layers[2].name == "inner" &&
              layers[3].name == "root",
          "asymmetric order changed");
  const auto &lo = named(prepared, "bottom"), &hi = named(prepared, "top");
  require(lo.visible && hi.visible, "supported layer disabled");
  require(lo.values.base_color[0] == 1.6f && lo.values.base_color[1] == .3f &&
              hi.values.base_color[2] == 3.6f,
          "explicit color transform not used");
  require(lo.channels[4] && std::abs(lo.values.bump - .3f) < 1e-6f &&
              lo.blending[4] == LayerBlend::Replace,
          "zero-neutral height not migrated to native Bump");
  require(!prepared.material.set.displacement.channel && !lo.displacement.enabled,
          "height became displacement");
  require(std::abs(lo.opacity[2] - .4f) < 1e-6f && std::abs(hi.opacity[0] - .5f) < 1e-6f,
          "source/action/layer opacity lost");
  require(hi.mask && hi.mask->background == .4f, "scalar mask background lost");
  // Noncommuting colors: native postorder gives top over bottom, with scalar
  // mask and source opacity composing coverage. No renderer/external vendor reference.
  const float alpha = hi.opacity[0] * hi.mask->background;
  require(std::abs(std::lerp(lo.values.base_color[0], hi.values.base_color[0], alpha) - 1.32f) <
              1e-5f,
          "native asymmetric stack contract changed");
  require(prepared.partial && !prepared.appearance_verified &&
              has(prepared, "uniform-color-policy") && has(prepared, "stack-order-policy"),
          "inference not reported");
  for (const auto &d : prepared.diagnostics)
    require(!prepared.diagnostic_path(d).empty(), "diagnostic lost source path");
  TextureSet legacy_destination;
  legacy_destination.bump_origin = 0;
  auto adapted = spsm_material_for_destination(prepared, legacy_destination);
  require(adapted.set.bump_origin == 0 &&
              std::abs(adapted.set.layers[0].values.bump + .2f) < 1e-6f &&
              adapted.set.base_values.bump == 0,
          "destination Bump constant was not rebased");
  require(prepared.material.set.bump_origin == .5f &&
              std::abs(prepared.material.set.layers[0].values.bump - .3f) < 1e-6f,
          "destination adaptation modified prepared source");
  TextureSet canonical_destination;
  auto canonical = spsm_material_for_destination(prepared, canonical_destination);
  require(canonical.set.bump_origin == .5f &&
              canonical.set.layers[0].values.bump == prepared.material.set.layers[0].values.bump,
          "canonical destination changed source coordinate");
  Builder bad;
  auto unknown = bad.object(
      "DataActionGroup",
      {{"subStack",
        bad.stack("DataStackActions", {bad.object("DataActionFilterProcedural", {})})}});
  auto masked = bad.layer("masked", {bad.fill({bad.uniform(1, {.2f, .2f, .2f})})}, {unknown}, 0);
  auto multiple = bad.layer(
      "multiple",
      {bad.fill({bad.uniform(1, {.3f, .3f, .3f})}), bad.fill({bad.uniform(1, {.8f, .8f, .8f})})});
  auto no_color = bad.layer(
      "no color",
      {bad.fill({bad.uniform(1, {.4f, .4f, .4f}), bad.uniform(128, {.7f, .7f, .7f})})});
  auto aux = bad.layer("auxiliary", {bad.fill({bad.uniform(0, {.9f, .9f, .9f})})});
  auto n = prepare_spsm_material(
      sem::inspect_archive(bad.archive(bad.folder("root", {masked, multiple, no_color, aux}))));
  require(named(n, "masked").visible && named(n, "masked").mask->enabled &&
              named(n, "masked").mask->background == 0,
          "unsupported mask became an unmasked visible layer");
  require(named(n, "multiple").visible && named(n, "auxiliary").visible,
          "multiple fills/auxiliary source silently replaced");
  require(named(n, "no color").visible && !named(n, "no color").channels[0] &&
              named(n, "no color").channels[2],
          "missing color policy lost supported scalar channel");
  require(has(n, "pending-mask-action") && has(n, "pending-content-action") &&
              has(n, "uniform-color-transform-missing") && has(n, "auxiliary-content-source"),
          "omission diagnostics missing");
  // Blend constants are mapped by name, never native ordinal. Signed Bump Add
  // must subtract native neutral exactly once.
  Builder blend;
  auto add = blend.layer("heightadd",
                         {blend.fill({blend.uniform(2, {.2f, .2f, .2f})}, {blend.blend(2, 10)})},
                         {},
                         1,
                         {blend.blend(2, 10)});
  auto ao = blend.layer(
      "ao",
      {blend.fill({blend.uniform(8388608, {.6f, .6f, .6f})}, {blend.blend(8388608, 4)})},
      {},
      1,
      {blend.blend(8388608, 4)});
  auto norm = blend.layer("normal",
                          {blend.fill({blend.uniform(4194304, {.5f, .5f, 1})})},
                          {},
                          1,
                          {blend.blend(4194304, 27)});
  auto mapped = prepare_spsm_material(
      sem::inspect_archive(blend.archive(blend.folder("root", {add, ao, norm}))));
  require(named(mapped, "heightadd").blending[4] == LayerBlend::SignedAddition &&
              std::abs(blend_value(named(mapped, "heightadd").blending[4],
                                   named(mapped, "heightadd").values.bump,
                                   .6f) -
                       .8f) < 1e-6f,
          "signed Bump Add used wrong neutral");
  require(named(mapped, "ao").ao_blending == LayerBlend::Multiply &&
              mapped.material.set.ao_channel,
          "AO Multiply mapping wrong");
  require(named(mapped, "normal").visible &&
              named(mapped, "normal").values.normal == std::array<float, 3>{0, 0, 1},
          "unverified normal decoded/switches format");
  auto rebased = spsm_material_for_destination(mapped, legacy_destination);
  require(rebased.set.layers[0].blending[4] == LayerBlend::LinearDodge &&
              std::abs(rebased.set.layers[0].values.bump - .2f) < 1e-6f &&
              mapped.material.set.layers[0].blending[4] == LayerBlend::SignedAddition,
          "legacy signed addition did not become signed-domain Add");
  bool bad_destination = false;
  legacy_destination.bump_origin = .25f;
  try {
    spsm_material_for_destination(prepared, legacy_destination);
  }
  catch (const s::Error &) {
    bad_destination = true;
  }
  require(bad_destination && legacy_destination.bump_origin == .25f,
          "unsupported destination coordinate mutated or accepted");
  legacy_destination.bump_origin = 0;
  auto unsupported = prepared;
  unsupported.material.set.layers[0].blending[4] = LayerBlend::Multiply;
  bool bad_blend = false;
  try {
    spsm_material_for_destination(unsupported, legacy_destination);
  }
  catch (const s::Error &) {
    bad_blend = true;
  }
  require(bad_blend && unsupported.material.set.layers[0].blending[4] == LayerBlend::Multiply,
          "unsupported signed-domain blend silently adapted");
  Builder duplicate;
  auto dup = duplicate.layer("dup",
                             {duplicate.fill({duplicate.uniform(128, {.2f, .2f, .2f}),
                                              duplicate.uniform(128, {.9f, .9f, .9f})})});
  auto du = prepare_spsm_material(
      sem::inspect_archive(duplicate.archive(duplicate.folder("root", {dup}))));
  require(named(du, "dup").visible && has(du, "duplicate-source-channel"),
          "duplicate channel overwrote first");
  bool denied = false;
  try {
    prepare_spsm_material(sem::Inspection(original), {});
  }
  catch (const s::Error &) {
    denied = true;
  }
  require(!denied, "Low telemetry blocked lowering");
  {
    TextureRamQueryScope ram([]() -> std::uint64_t { return 32; });
    denied = false;
    try {
      prepare_spsm_material(sem::Inspection(original), {});
    }
    catch (const s::Error &) {
      denied = true;
    }
    require(!denied, "Low telemetry blocked lowering");
  }
  Builder malformed;
  auto invalid = malformed.layer(
      "invalid",
      {malformed.fill(
          {malformed.uniform(128, {.2f, .2f, .2f}, std::numeric_limits<float>::quiet_NaN())})});
  denied = false;
  try {
    prepare_spsm_material(
        sem::inspect_archive(malformed.archive(malformed.folder("root", {invalid}))));
  }
  catch (const s::Error &) {
    denied = true;
  }
  require(denied, "nonfinite opacity accepted");
  Builder nul;
  auto nul_label = nul.layer(std::string("invalid\0label", 13),
                             {nul.fill({nul.uniform(128, {.2f, .2f, .2f})})});
  denied = false;
  try {
    prepare_spsm_material(sem::inspect_archive(nul.archive(nul.folder("root", {nul_label}))));
  }
  catch (const s::Error &) {
    denied = true;
  }
  require(denied, "native-unserializable NUL label accepted");
  Builder bounds;
  auto too_big = bounds.layer(
      "range", {bounds.fill({bounds.uniform(128, {2, 2, 2}), bounds.uniform(1, {70000, 0, 0})})});
  auto range = prepare_spsm_material(
      sem::inspect_archive(bounds.archive(bounds.folder("root", {too_big}))), color_policy());
  require(named(range, "range").visible && has(range, "native-scalar-domain") &&
              has(range, "native-color-domain"),
          "native serialization domains silently clamped/overflowed");
  Builder cycle;
  auto cyclic = cycle.layer("cycle", {cycle.fill({cycle.uniform(128, {.2f, .2f, .2f})})});
  auto cyclic_ir = sem::inspect_archive(cycle.archive(cycle.folder("root", {cyclic})));
  cyclic_ir.presets[0].actions[0].children.push_back(0);
  denied = false;
  try {
    prepare_spsm_material(std::move(cyclic_ir));
  }
  catch (const s::Error &) {
    denied = true;
  }
  require(denied, "mutated cyclic action IR accepted");
  Builder culling;
  auto uv_fill = culling.fill({culling.uniform(128, {.2f, .2f, .2f})});
  auto spatial_fill = culling.fill({culling.uniform(128, {.3f, .3f, .3f})});
  const auto uv_depth = culling.boolean(true), spatial_depth = culling.boolean(true);
  std::get<s::Object>(culling.p.nodes[uv_fill].value).fields.push_back({"depthCulling", uv_depth});
  std::get<s::Object>(culling.p.nodes[spatial_fill].value)
      .fields.push_back({"depthCulling", spatial_depth});
  for (auto &f : std::get<s::Object>(culling.p.nodes[uv_fill].value).fields)
    if (f.name == "projection")
      culling.p.nodes[f.value].value = std::uint64_t(0);
  auto uv = culling.layer("uv", {uv_fill});
  auto spatial = culling.layer("spatial", {spatial_fill});
  auto coverage = prepare_spsm_material(
      sem::inspect_archive(culling.archive(culling.folder("root", {uv, spatial}))));
  require(named(coverage, "uv").visible && named(coverage, "spatial").visible &&
              has(coverage, "uv-culling-policy") && has(coverage, "fill-culling"),
          "UV culling policy silently changed spatial coverage");
  Builder deep;
  auto leaf = deep.layer("leaf", {deep.fill({deep.uniform(128, {.4f, .4f, .4f})})});
  for (unsigned i = 0; i < 2048; ++i)
    leaf = deep.folder("nested", {leaf});
  auto nested = prepare_spsm_material(sem::inspect_archive(deep.archive(leaf)));
  hierarchy(nested);
  require(nested.material.set.layers.size() == 2049 &&
              nested.material.set.layers.front().depth == 2048,
          "deep source hierarchy truncated");
  std::cout
      << "PASS native lowering: asymmetric postorder/nesting, actual native uniforms, explicit color callback, opacity/scalar masks, signed Bump/AO blends, authored visibility for unsupported subactions, immutable source, RAM/boundaries, 2048 groups\n";
}
void native_semantics()
{
  Builder b;
  const auto tweak = [&](const char *name, float value) {
    return b.object("DataTweakFloat", {{"identifier", b.text(name)}, {"value", b.scalar(value)}});
  };
  const auto proc = b.object("DataProcedural",
                             {{"urlToSbsRes", b.text("pkg://mg_mask_builder")},
                              {"tweaks",
                               b.array({tweak("Level", .35f),
                                        tweak("Contrast", .7f),
                                        tweak("Grunge Amount", .4f),
                                        tweak("Grunge_Scale", 11)})}});
  const auto source = b.object(
      "DataSourceProcedural",
      {{"channelTypes", b.integer(0)}, {"procedural", proc}, {"opacity", b.scalar(1)}});
  const auto generator = b.fill({source}, {b.blend(0, 2, .86f)});
  const auto unsupported = b.object("DataActionPaint", {{"enabled", b.boolean(true)}});
  const auto first = b.fill({b.uniform(128, {.2f, .2f, .2f})});
  const auto second = b.fill({b.uniform(128, {.8f, .8f, .8f})}, {b.blend(128, 11, .22f)});
  const auto layer = b.layer("native", {first, second, unsupported}, {generator}, 0);
  const auto group_fill = b.fill({b.uniform(0, {.8f, .8f, .8f})});
  const auto group = b.object("DataActionGroup",
                              {{"enabled", b.boolean(true)},
                               {"perChannelBlending", b.array({b.blend(0, 2, .25f)})},
                               {"subStack", b.stack("DataStackActions", {group_fill})}});
  const auto grouped = b.layer(
      "grouped", {b.fill({b.uniform(128, {.4f, .4f, .4f})})}, {group}, .6f);
  auto options = color_policy();
  options.native_actions = true;
  auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {layer, grouped}))), options);
  const auto &out = named(prepared, "native");
  require(out.visible && out.channels[2] && !out.channels[3],
          "Unsupported subaction hid native layer or exposed Normal");
  require(out.effects.size() == 2 && out.effects.front().initialize_content &&
              out.effects.back().kind == ContentEffectKind::Fill &&
              out.effects.back().blending == LayerBlend::Overlay &&
              std::abs(out.effects.back().opacity - .22f) < 1e-6f,
          "Ordered Fill blend/opacity lost");
  float coverage = 1;
  const auto value = apply_content_effects(
      out.effects, 2, {.2f, .2f, .2f}, coverage, 4, 4, 1001, 16, 16, ChannelPrecision::L32F);
  require(std::abs(value[0] - std::lerp(.2f, blend_value(LayerBlend::Overlay, .8f, .2f), .22f)) <
              1e-6f,
          "Noncommuting Fill evaluation lost order");
  require(out.mask && out.mask->effects.size() == 1, "Native mask generator omitted");
  const auto &effect = out.mask->effects.front();
  require(effect.generator == 8 && std::abs(effect.opacity - .86f) < 1e-6f &&
              effect.grunge_scale == 11 && std::abs(effect.grunge_amount - .4f) < 1e-6f,
          "Typed source parameters or action opacity lost");
  require(effect.surface_fallback && !effect.triplanar_grunge,
          "New imported generator lost fallback/native UV policy");
  MeshMaps maps;
  for (int role : {3, 4}) {
    MeshMap map;
    map.name = "semantic fixture";
    std::vector<float> rgba(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        const auto at = (y * 32 + x) * 4;
        rgba[at] = role == 3 ? 1.f : x < 16 ? .5f : 1.f;
        rgba[at + 3] = 1;
      }
    auto pixels = std::make_shared<MeshMapPixels>(32, std::move(rgba));
    map.tiles[1001] = {32, pixels->hash, pixels};
    maps[role] = std::move(map);
  }
  const auto flat = generated_mask(effect, &maps, .25, .5, 1001),
             edge = generated_mask(effect, &maps, .75, .5, 1001);
  require(flat && edge && *edge > *flat + .5f, "Edge wear did not respond to live curvature");
  const auto codec = read_mask_effects(mask_effects_json(out.mask->effects));
  require(codec.size() == 1 && codec[0].generator == 8 && codec[0].grunge_scale == 11,
          "Native generator settings lost codec roundtrip");
  require(codec[0].response == effect.response, "Painter response lost through the mask codec");
  auto extended = effect;
  extended.curvature_radius = 2.3f;
  extended.response = 2;
  const auto full_codec = read_mask_effects(mask_effects_json({extended}));
  require(full_codec[0].response == 2 && full_codec[0].curvature_radius == 2.3f,
          "Mask preset discarded response or curvature bands");
  const auto &isolated = named(prepared, "grouped");
  require(isolated.mask && isolated.mask->effects.front().kind == MaskEffectKind::Group,
          "Nested mask group flattened");
  require(std::abs(isolated.mask->value(2, 2, 1001, 16, 16) - .65f) < 1e-6f,
          "Isolated group blend/opacity incorrect");
  const auto group_codec = read_mask_effects(mask_effects_json(isolated.mask->effects));
  LayerMask reopened = *isolated.mask;
  reopened.effects = group_codec;
  require(std::abs(reopened.value(2, 2, 1001, 16, 16) - .65f) < 1e-6f,
          "Nested mask group codec lost behavior");
  MeshGeneratorSettings noise;
  noise.generator = 7;
  noise.grunge_scale = 13;
  noise.grunge_seed = 41;
  const auto a = generated_mask(noise, nullptr, .1, .2, 1001),
             repeat = generated_mask(noise, nullptr, .1, .2, 1001),
             other = generated_mask(noise, nullptr, .7, .8, 1001);
  require(a && repeat && other && *a == *repeat && std::abs(*a - *other) > .01f,
          "Native grunge is not deterministic/spatial");
  FilterSettings warp;
  warp.resource = 9;
  warp.value_scale = .37f;
  FilterSettings slope = warp;
  slope.resource = 10;
  require(read_filter(filter_json(warp)).resource == 9 &&
              read_filter(filter_json(slope)).value_scale == .37f,
          "Native Warp/Slope Blur settings lost roundtrip");
  FilterSettings hsl;
  hsl.resource = 4;
  hsl.saturation = 0;
  const auto neutral = filter_rgb(hsl, {1, 0, 0});
  require(neutral[0] == neutral[1] && neutral[1] == neutral[2],
          "Native HSL scalar conversion regressed");
  bool truncated = false;
  try {
    spsm_preview_detail::encoded_preview(std::array<std::uint8_t, 4>{0, 0, 0, 1});
  }
  catch (const s::Error &) {
    truncated = true;
  }
  require(truncated, "Truncated preview metadata was accepted");
  require(has(prepared, "native-subaction-omitted") &&
              has(prepared, "native-procedural-approximation"),
          "Approximation/omission honesty lost");
  std::cout
      << "PASS native semantic actions: visibility, ordered Overlay Fill, typed edge wear, live curvature response, isolated mask group codec, deterministic grunge, internal Normal\n";
}
void advanced_ordered_actions()
{
  Builder b;
  const auto levels = [&](std::uint64_t channels, std::vector<s::NodeId> mix) {
    const auto curve = b.object("DataLevels",
                                {{"clamp", b.integer(1)},
                                 {"gamma", b.rgb({2, 2, 2})},
                                 {"inputMinimum", b.rgb({0, 0, 0})},
                                 {"inputMaximum", b.rgb({1, 1, 1})},
                                 {"outputMinimum", b.rgb({0, 0, 0})},
                                 {"outputMaximum", b.rgb({1, 1, 1})}});
    return b.object("DataActionFilterLevels",
                    {{"channelTypes", b.integer(channels)},
                     {"enabled", b.boolean(true)},
                     {"levels", curve},
                     {"perChannelBlending", b.array(std::move(mix))}});
  };
  const auto filter =
      [&](const char *uri, std::uint64_t channels, std::uint64_t mode, float opacity) {
        const auto proc = b.object("DataProcedural",
                                   {{"urlToSbsRes", b.text(uri)}, {"tweaks", b.array({})}});
        return b.object("DataActionFilterProcedural",
                        {{"channelTypes", b.integer(channels)},
                         {"enabled", b.boolean(true)},
                         {"procedural", proc},
                         {"perChannelBlending", b.array({b.blend(channels, mode, opacity)})}});
      };
  std::vector<s::NodeId> layers;
  for (int n = 0; n < 13; ++n) {
    const auto first = b.fill({b.uniform(1, {.2f, .3f, .4f}),
                               b.uniform(128, {.25f, .25f, .25f}),
                               b.uniform(8192, {.6f, .6f, .6f})});
    const auto second = b.fill({b.uniform(128, {.6f, .6f, .6f}, .8f)}, {b.blend(128, 15, .5f)});
    const auto curve = levels(128 | 8192, {b.blend(128, 4, .3f), b.blend(8192, 11, .7f)});
    const auto maskcurve = levels(0, {b.blend(0, 15, .4f)});
    layers.push_back(b.layer("detail " + std::to_string(n),
                             {first,
                              second,
                              curve,
                              filter("pkg://hsl_perceptive", 1, 11, .2f),
                              filter("pkg://fx_sharpen", 128, 10, .3f),
                              filter("pkg://blur", 128, 4, .4f),
                              filter("pkg://blur_slope", 128, 15, .5f),
                              filter("pkg://fx_warp", 128, 11, .6f)},
                             {b.fill({b.uniform(0, {.8f, .8f, .8f})}),
                              b.fill({b.uniform(0, {.3f, .3f, .3f})}, {b.blend(0, 4, .7f)}),
                              maskcurve,
                              filter("pkg://fx_warp", 0, 15, .6f)},
                             1));
  }
  auto options = color_policy();
  options.native_actions = true;
  auto prepared = prepare_spsm_material(sem::inspect_archive(b.archive(b.folder("root", layers))),
                                        options);
  require(prepared.layers.size() == 14, "Thirteen detail layers or their folder were lost");
  hierarchy(prepared);
  for (const auto &out : prepared.material.set.layers)
    if (out.kind == LayerKind::Fill) {
      require(out.visible && out.effects.size() == 11,
              "Secondary fills, channel Levels or filter detail disappeared");
      require(out.effects[3].kind == ContentEffectKind::Fill &&
                  out.effects[3].blending == LayerBlend::LinearLight &&
                  std::abs(out.effects[3].opacity - .4f) < 1e-6f,
              "Secondary Fill blend/source opacity lost");
      require(out.effects[4].channel == 1 && out.effects[4].blending == LayerBlend::Overlay &&
                  out.effects[4].opacity == .7f && out.effects[5].channel == 2 &&
                  out.effects[5].blending == LayerBlend::Multiply && out.effects[5].opacity == .3f,
              "Per-channel Levels blend/opacity lost");
      const int resources[]{4, 8, 7, 10, 9};
      for (int i = 0; i < 5; ++i)
        require(out.effects[6 + i].kind == ContentEffectKind::Filter &&
                    out.effects[6 + i].filter.resource == resources[i],
                "Filter action order changed");
      const auto saved = read_effects(effects_json(out.effects));
      require(saved.size() == out.effects.size() && saved[3].blending == LayerBlend::LinearLight &&
                  saved[10].opacity == .6f,
              "Ordered detail codec lost blend/opacity");
      std::vector<LevelsEffect> point(out.effects.begin(), out.effects.begin() + 6);
      float coverage = 1;
      const auto value = apply_content_effects(
          point, 2, {0, 0, 0}, coverage, 0, 0, 1001, 16, 16, ChannelPrecision::L32F);
      // Initial .25, LinearLight .6 at coverage .4, then Multiply sqrt at .3.
      const float filled = .25f * .6f + (.25f + 2 * .6f - 1) * .4f;
      const float expected = filled * .7f + filled * std::sqrt(filled) * .3f;
      require(std::abs(value[0] - expected) < 1e-6f,
              "Noncommuting secondary Fill/Levels result changed");
      require(out.mask->effects.size() == 4 &&
                  out.mask->effects[1].blending == LayerBlend::Multiply &&
                  out.mask->effects[2].blending == LayerBlend::LinearLight &&
                  out.mask->effects[3].filter.resource == 9,
              "Mask multi-fill/Levels/filter order changed");
    }
  require(std::all_of(prepared.actions.begin(),
                      prepared.actions.end(),
                      [](const auto &a) { return a.lowered; }),
          "Supported advanced subaction remained omitted");
  std::cout
      << "PASS 13 detail layers: multi-Fill, multi-channel Levels, ordered HSL/Sharpen/Blur/Slope Blur/Warp, exact blend modes/opacities, codec and noncommuting CPU result\n";
}
void levels_channel_isolation()
{
  Builder b;
  const auto gamma = b.rgb({2, 1, .5f});
  const auto curve = b.object("DataLevels",
                              {{"clamp", b.integer(1)},
                               {"gamma", gamma},
                               {"inputMinimum", b.rgb({0, 0, 0})},
                               {"inputMaximum", b.rgb({1, 1, 1})},
                               {"outputMinimum", b.rgb({0, 0, 0})},
                               {"outputMaximum", b.rgb({1, 1, 1})}});
  const auto levels = b.object(
      "DataActionFilterLevels",
      {{"channelTypes", b.integer(1 | 128)},
       {"enabled", b.boolean(true)},
       {"levels", curve},
       {"perChannelBlending", b.array({b.blend(1, 11, .4f), b.blend(128, 4, .7f)})}});
  const auto first = b.fill({b.uniform(1, {.25f, .4f, .6f}), b.uniform(128, {.3f, .3f, .3f})});
  const auto last = b.fill({b.uniform(1, {.1f, .2f, .3f}, .5f), b.uniform(128, {.8f, .8f, .8f})},
                           {b.blend(1, 4, .2f), b.blend(128, 4, .5f)});
  const auto layer = b.layer("partial Levels", {first, levels, last});
  auto options = color_policy();
  options.native_actions = true;
  options.uniform_rgb_to_scene_linear = [](auto rgb) { return rgb; };
  const auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {layer}))), options);
  const auto &out = named(prepared, "partial Levels");
  require(out.effects.size() == 5, "Unsupported scalar Levels discarded the valid Color channel");
  require(out.effects[2].channel == 0 && out.effects[2].blending == LayerBlend::Overlay &&
              out.effects[2].opacity == .4f,
          "Valid sibling Levels lost action position or mix");
  require(prepared.partial && has(prepared, "native-levels-roughness-omitted"),
          "Unsupported scalar curve lacks independent omission");
  require(std::any_of(prepared.diagnostics.begin(),
                      prepared.diagnostics.end(),
                      [&](const auto &d) {
                        return d.source == gamma && d.code == "native-levels-roughness-omitted" &&
                               prepared.diagnostic_path(d).find("gamma") != std::string::npos;
                      }),
          "Unsupported curve diagnostic lost exact parameter path");
  const auto check = [&](const auto &effects) {
    float coverage = 1;
    const auto color = apply_content_effects(
        effects, 0, {0, 0, 0}, coverage, 0, 0, 1001, 16, 16, ChannelPrecision::RGB32F);
    const std::array<float, 3> initial{.25f, .4f, .6f}, mapped{.5f, .4f, .36f},
        following{.1f, .2f, .3f};
    for (int c = 0; c < 3; ++c) {
      const float mixed = std::lerp(
          initial[c], blend_value(LayerBlend::Overlay, mapped[c], initial[c]), .4f);
      const float expected = std::lerp(mixed, mixed * following[c], .1f);
      require(std::abs(color[c] - expected) < 1e-6f,
              "Partial RGB Levels chain changed numerical source order");
    }
    coverage = 1;
    const auto rough = apply_content_effects(
        effects, 2, {0, 0, 0}, coverage, 0, 0, 1001, 16, 16, ChannelPrecision::L32F);
    require(std::abs(rough[0] - .27f) < 1e-6f, "Unsupported scalar curve affected following Fill");
  };
  check(out.effects);
  check(read_effects(effects_json(out.effects)));
  std::cout
      << "PASS per-channel Levels isolation: valid RGB retained, unsupported scalar diagnosed at gamma, ordered following Fill and codec arithmetic\n";
}
void mask_levels_sentinels()
{
  for (const auto action_channels : {std::uint64_t(0), UINT64_MAX})
    for (const auto mix_channels : {std::uint64_t(0), UINT64_MAX})
      for (const auto source_mode : {11u, 4u, 10u, 15u}) {
        Builder b;
        const auto curve = b.object("DataLevels",
                                    {{"clamp", b.integer(1)},
                                     {"gamma", b.rgb({2, 2, 2})},
                                     {"inputMinimum", b.rgb({0, 0, 0})},
                                     {"inputMaximum", b.rgb({1, 1, 1})},
                                     {"outputMinimum", b.rgb({0, 0, 0})},
                                     {"outputMaximum", b.rgb({1, 1, 1})}});
        const auto levels = b.object(
            "DataActionFilterLevels",
            {{"channelTypes", b.integer(action_channels)},
             {"enabled", b.boolean(true)},
             {"levels", curve},
             {"perChannelBlending", b.array({b.blend(mix_channels, source_mode, .4f)})}});
        const auto group = b.object("DataActionGroup",
                                    {{"enabled", b.boolean(true)},
                                     {"perChannelBlending", b.array({b.blend(0, 25, .5f)})},
                                     {"subStack", b.stack("DataStackActions", {levels})}});
        const auto layer = b.layer(
            "mask Levels",
            {b.fill({b.uniform(128, {.3f, .3f, .3f})})},
            {b.fill({b.uniform(0, {.36f, .36f, .36f})}),
             group,
             b.fill({b.uniform(0, {.2f, .2f, .2f})}, {b.blend(0, 4, .7f)})});
        auto options = color_policy();
        options.native_actions = true;
        const auto prepared = prepare_spsm_material(
            sem::inspect_archive(b.archive(b.folder("root", {layer}))), options);
        const auto &out = named(prepared, "mask Levels");
        require(out.mask && out.mask->effects.size() == 3,
                "All-channel scalar mask Levels was discarded");
        const auto &effect = out.mask->effects[1];
        const auto mode = source_mode == 11 ? LayerBlend::Overlay :
                          source_mode == 4  ? LayerBlend::Multiply :
                          source_mode == 10 ? LayerBlend::LinearDodge :
                                              LayerBlend::LinearLight;
        require(effect.kind == MaskEffectKind::Levels && effect.blending == mode &&
                    std::abs(effect.opacity - .2f) < 1e-6f,
                "Mask Levels group/action mix lost");
        const float mixed = std::lerp(.36f, blend_value(mode, .6f, .36f), .2f),
                    expected = std::lerp(mixed, mixed * .2f, .7f);
        require(std::abs(out.mask->value(0, 0, 1001, 16, 16) - expected) < 1e-6f,
                "Mask Levels numerical source order changed");
        auto reopened = *out.mask;
        reopened.effects = read_mask_effects(mask_effects_json(out.mask->effects));
        require(std::abs(reopened.value(0, 0, 1001, 16, 16) - expected) < 1e-6f,
                "Mask Levels codec changed point result");
        if (action_channels == UINT64_MAX)
          require(has(prepared, "native-mask-all-channels-levels"),
                  "All-channel mask Levels interpretation lacks diagnostic");
        const auto pi = *prepared.source->primary_preset;
        const auto &ir = prepared.source->presets[pi];
        const auto found = std::find_if(ir.actions.begin(), ir.actions.end(), [&](const auto &a) {
          return a.source == levels;
        });
        require(found != ir.actions.end(), "Levels source action missing");
        bool strict = false;
        try {
          parse_spsm_scalar_levels(prepared.source->source().presets[pi],
                                   *found,
                                   SpsmLevelsPolicy::NativeScalarLevels,
                                   SpsmScalarDomain::MaskAuxiliary0);
        }
        catch (const SpsmLevelsUnsupported &) {
          strict = true;
        }
        require(strict, "Native mask sentinel widened strict standalone admission");
      }
  std::cout
      << "PASS scalar mask Levels: 16 sentinel/mode combinations, nested opacity, following Fill, codec and strict standalone boundary\n";
}
void mask_levels_admission()
{
  // All-channel is a mask-context sentinel, not permission to guess arbitrary
  // channel selectors or malformed mix/curve metadata.
  for (int invalid = 0; invalid < 8; ++invalid) {
    Builder b;
    const auto opacity = invalid == 3 ?
                             b.boolean(true) :
                             b.scalar(invalid == 4 ? std::numeric_limits<float>::quiet_NaN() :
                                                     .4f);
    const auto row = b.object("DataBlending",
                              {{"channelTypes", b.integer(invalid == 0 ? 1 : UINT64_MAX)},
                               {"blendingMode", b.integer(invalid == 2 ? 777 : 11)},
                               {"opacity", opacity}});
    std::vector<s::NodeId> rows{row};
    if (invalid == 1)
      rows.push_back(b.blend(0));
    const auto curve = b.object(
        "DataLevels",
        {{"clamp", b.integer(1)},
         {"gamma",
          b.rgb(invalid == 6 ? std::array<float, 3>{2, 1, 2} : std::array<float, 3>{2, 2, 2})},
         {"inputMinimum", b.rgb({0, 0, 0})},
         {"inputMaximum", b.rgb({1, 1, 1})},
         {"outputMinimum", b.rgb({0, 0, 0})},
         {"outputMaximum", b.rgb({1, 1, 1})}});
    const auto levels = b.object("DataActionFilterLevels",
                                 {{"channelTypes", b.integer(invalid == 5 ? 1 : UINT64_MAX)},
                                  {"enabled", b.boolean(true)},
                                  {"levels", curve},
                                  {"perChannelBlending", b.array(rows)}});
    if (invalid == 7)
      std::get<s::Object>(b.p.nodes[curve].value)
          .fields.push_back({"futureFormula", b.integer(1)});
    const auto layer = b.layer("invalid mask Levels",
                               {b.fill({b.uniform(128, {.3f, .3f, .3f})})},
                               {b.fill({b.uniform(0, {.36f, .36f, .36f})}),
                                levels,
                                b.fill({b.uniform(0, {.2f, .2f, .2f})}, {b.blend(0, 4, .7f)})});
    auto options = color_policy();
    options.native_actions = true;
    const auto result = prepare_spsm_material(
        sem::inspect_archive(b.archive(b.folder("root", {layer}))), options);
    const auto &out = named(result, "invalid mask Levels");
    require(out.mask && out.mask->effects.size() == 2 && has(result, "native-subaction-omitted"),
            "Malformed mask Levels admitted or following Fill lost");
    if (invalid < 5)
      require(has(result, "native-levels-mix-unsupported"),
              "Malformed Levels mix lacks diagnostic");
    require(std::abs(out.mask->value(0, 0, 1001, 16, 16) - std::lerp(.36f, .36f * .2f, .7f)) <
                1e-6f,
            "Rejected mask Levels changed sibling numerical chain");
  }
  std::cout
      << "PASS mask Levels admission rejects unknown/overlapping selectors, unknown blend, boolean/NaN opacity, non-mask channels, RGB scalar ambiguity and unknown curve fields\n";
}
void procedural_source_color()
{
  for (const auto mode : {11u, 4u, 10u, 15u, 1u, 2u})
    for (float initial_coverage : {0.f, .45f, 1.f})
      for (float action_opacity : {0.f, .37f, 1.f}) {
        Builder b;
        const auto tint = b.object(
            "DataTweakColor",
            {{"identifier", b.text("base_color")}, {"value", b.rgb({.1f, .2f, .3f})}});
        const auto position = b.object(
            "DataTweakFloat3", {{"identifier", b.text("position")}, {"value", b.rgb({4, 5, 6})}});
        const auto proc = b.object("DataProcedural",
                                   {{"urlToSbsRes", b.text("pkg://grunge_noise")},
                                    {"tweaks", b.array({position, tint})}});
        const auto source = b.object(
            "DataSourceProcedural",
            {{"channelTypes", b.integer(1)}, {"procedural", proc}, {"opacity", b.scalar(.6f)}});
        const auto generated = b.fill({source}, {b.blend(1, mode, action_opacity)});
        const auto group = b.object("DataActionGroup",
                                    {{"enabled", b.boolean(true)},
                                     {"perChannelBlending", b.array({b.blend(1, 25, .25f)})},
                                     {"subStack", b.stack("DataStackActions", {generated})}});
        const auto first = b.fill({b.uniform(1, {.12f, .08f, .04f}, initial_coverage)});
        const auto last = b.fill({b.uniform(1, {.08f, .03f, .02f}, .7f)}, {b.blend(1, 4, .4f)});
        const auto layer = b.layer("colored source", {first, group, last});
        auto options = color_policy();
        options.native_actions = true;
        const auto prepared = prepare_spsm_material(
            sem::inspect_archive(b.archive(b.folder("root", {layer}))), options);
        const auto &out = named(prepared, "colored source");
        const auto effect = std::find_if(
            out.effects.begin(), out.effects.end(), [](const auto &e) {
              return e.kind == ContentEffectKind::Generator;
            });
        require(effect != out.effects.end(), "Colored procedural source lost generator");
        require(
            effect->generator_color == std::optional(std::array<float, 3>{.2f, .6f, 1.2f}) &&
                has(prepared, "native-generator-color-source"),
            "Named Color control was not converted once, or arbitrary Float3 was used as Color");
        require(std::none_of(prepared.diagnostics.begin(),
                             prepared.diagnostics.end(),
                             [&](const auto &d) {
                               return d.source == tint &&
                                      d.code == "native-procedural-parameter-omitted";
                             }),
                "Consumed source Color was reported as omitted");
        const auto g = generated_mask(*effect, nullptr, 7.5 / 16, 4.5 / 16, 1001);
        require(g.has_value(), "Colored procedural CPU generator unavailable");
        std::array<float, 3> expected{.24f, .24f, .16f};
        float coverage = initial_coverage;
        const auto composite = [&](const std::array<float, 3> &ink,
                                   float alpha,
                                   std::uint64_t blend) {
          const float next = alpha + coverage * (1 - alpha);
          if (next > 0)
            for (int c = 0; c < 3; ++c) {
              const float a = ink[c], back = expected[c];
              const float mixed = blend == 11 ?
                                      (back <= .5f ? 2 * back * a : 1 - 2 * (1 - back) * (1 - a)) :
                                  blend == 4  ? back * a :
                                  blend == 10 ? back + a :
                                  blend == 15 ? back + 2 * a - 1 :
                                                a;
              expected[c] = ((1 - alpha) * back * coverage +
                             alpha * ((1 - coverage) * a + coverage * mixed)) /
                            next;
            }
          coverage = next;
        };
        // Named Color conversion occurs once on the source color; RGB tint precedes
        // action blend/opacity and does not change previous content or its coverage.
        composite({*g * .2f, *g * .6f, *g * 1.2f}, .6f * action_opacity * .25f, mode);
        composite({.16f, .09f, .08f}, .7f * .4f, 4);
        float actual_coverage = 1;
        const auto actual = apply_content_effects(out.effects,
                                                  0,
                                                  {0, 0, 0},
                                                  actual_coverage,
                                                  7,
                                                  4,
                                                  1001,
                                                  16,
                                                  16,
                                                  ChannelPrecision::RGB32F);
        require(std::abs(actual_coverage - coverage) < 1e-6f,
                "Procedural color changed coverage outside its source action");
        for (int c = 0; c < 3; ++c)
          require(std::abs(actual[c] - expected[c]) < 1e-6f,
                  "Procedural tint recolored backdrop or bypassed source/action/group mix");
        require(out.effects.size() == 3,
                "Procedural source color escaped as a post-composite Multiply effect");
        const auto decoded = read_effects(effects_json(out.effects));
        float saved_coverage = 1;
        const auto saved = apply_content_effects(
            decoded, 0, {0, 0, 0}, saved_coverage, 7, 4, 1001, 16, 16, ChannelPrecision::RGB32F);
        require(saved == actual && saved_coverage == actual_coverage,
                "Procedural source color lost codec behavior");
      }
  LevelsEffect legacy;
  legacy.kind = ContentEffectKind::Generator;
  legacy.generator = 7;
  require(effects_json({legacy})[0].toObject()["type"] == "generator" &&
              !read_effects(effects_json({legacy}))[0].generator_color,
          "Legacy scalar generator format changed");
  auto colored = legacy;
  colored.generator_color = std::array<float, 3>{-.1f, .6f, 1.2f};
  const auto object = effects_json({colored})[0].toObject();
  require(object["type"] == "colored_generator",
          "Colored generator is silently readable as an old scalar-only record");
  for (int problem = 0; problem < 6; ++problem) {
    auto bad = object;
    if (problem == 0)
      bad["channel"] = 2;
    else if (problem == 1)
      bad.remove("color");
    else if (problem == 2)
      bad["color"] = QJsonArray{1, 2};
    else if (problem == 3)
      bad["color"] = QJsonArray{1, "bad", 3};
    else if (problem == 4)
      bad["color"] = QJsonArray{1, 65505, 3};
    else
      bad["type"] = "generator";
    bool rejected = false;
    try {
      read_effects(QJsonArray{bad});
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "Malformed or downgraded colored generator was accepted");
  }
  require(read_effects(effects_json({colored}))[0].generator_color == colored.generator_color,
          "Signed/HDR generator color lost codec roundtrip");
  colored.channel = 2;
  bool rejected = false;
  try {
    effects_json({colored});
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Scalar generator serialized a Color tint");
  std::cout
      << "PASS procedural source color: six blends, zero/partial/full backdrop and action coverage, named HDR Color conversion once, nested/source opacity and following Fill, exact codec behavior\n";
}
void procedural_color_controls()
{
  for (int policy = 0; policy < 3; ++policy) {
    Builder b;
    const auto tint = [&](const char *name) {
      return b.object("DataTweakColor",
                      {{"identifier", b.text(name)}, {"value", b.rgb({.1f, .2f, .3f})}});
    };
    std::vector<s::NodeId> tweaks{tint("color")};
    if (policy == 1)
      tweaks.push_back(tint("diffuse_color"));
    const auto proc = b.object(
        "DataProcedural",
        {{"urlToSbsRes", b.text("pkg://grunge_noise")}, {"tweaks", b.array(std::move(tweaks))}});
    const auto source = b.object("DataSourceProcedural",
                                 {{"channelTypes", b.integer(1)}, {"procedural", proc}});
    const auto layer = b.layer("color controls", {b.fill({source})});
    auto options = color_policy();
    options.native_actions = true;
    if (policy == 0)
      options.uniform_rgb_to_scene_linear = {};
    else if (policy == 2)
      options.uniform_rgb_to_scene_linear = [](std::array<float, 3>) {
        return std::array<float, 3>{NAN, 0, 0};
      };
    const auto prepared = prepare_spsm_material(
        sem::inspect_archive(b.archive(b.folder("root", {layer}))), options);
    const auto &out = named(prepared, "color controls");
    const char *codes[]{"native-generator-color-transform-missing",
                        "native-generator-color-ambiguous",
                        "native-generator-color-invalid"};
    require(has(prepared, codes[policy]),
            "Missing/ambiguous/invalid source Color policy was hidden");
    require(out.effects.size() == 2 && out.effects.back().kind == ContentEffectKind::Generator &&
                !out.effects.back().generator_color,
            "Unsupported source Color silently tinted native content");
  }
  std::cout
      << "PASS procedural Color controls: missing named transform, ambiguous controls and invalid transform are retained/diagnosed independently\n";
}
void grouped_action_mixes()
{
  Builder b;
  constexpr std::array<std::uint64_t, 5> bits{1, 8192, 128, 2, 8388608};
  const auto group = [&](std::vector<s::NodeId> children, std::vector<s::NodeId> blends) {
    return b.object("DataActionGroup",
                    {{"enabled", b.boolean(true)},
                     {"perChannelBlending", b.array(std::move(blends))},
                     {"subStack", b.stack("DataStackActions", std::move(children))}});
  };
  const auto fill = [&](float value, float source_opacity, std::uint64_t mode, float opacity) {
    std::vector<s::NodeId> sources, blends;
    for (auto bit : bits) {
      sources.push_back(b.uniform(bit, {value, value, value}, source_opacity));
      blends.push_back(b.blend(bit, mode, opacity));
    }
    return b.fill(std::move(sources), std::move(blends));
  };
  const auto procedural =
      [&](const char *uri, std::uint64_t mode, float opacity, std::vector<s::NodeId> tweaks) {
        const auto proc = b.object(
            "DataProcedural",
            {{"urlToSbsRes", b.text(uri)}, {"tweaks", b.array(std::move(tweaks))}});
        std::vector<s::NodeId> blends;
        for (auto bit : bits)
          blends.push_back(b.blend(bit, mode, opacity));
        return b.object("DataActionFilterProcedural",
                        {{"channelTypes", b.integer(1 | 8192 | 128 | 2 | 8388608)},
                         {"procedural", proc},
                         {"perChannelBlending", b.array(std::move(blends))}});
      };
  const auto curve = b.object("DataLevels",
                              {{"clamp", b.integer(1)},
                               {"gamma", b.rgb({2, 2, 2})},
                               {"inputMinimum", b.rgb({0, 0, 0})},
                               {"inputMaximum", b.rgb({1, 1, 1})},
                               {"outputMinimum", b.rgb({0, 0, 0})},
                               {"outputMaximum", b.rgb({1, 1, 1})}});
  std::vector<s::NodeId> level_blends;
  for (auto bit : bits)
    level_blends.push_back(b.blend(bit, 4, .6f));
  const auto levels = b.object("DataActionFilterLevels",
                               {{"channelTypes", b.integer(1 | 8192 | 128 | 2 | 8388608)},
                                {"levels", curve},
                                {"perChannelBlending", b.array(std::move(level_blends))}});
  const auto lightness = b.object(
      "DataTweakFloat", {{"identifier", b.text("lightness")}, {"value", b.scalar(.25f)}});
  const auto inner = group({fill(.8f, .7f, 11, .8f),
                            levels,
                            procedural("pkg://hsl_perceptive", 10, .4f, {lightness}),
                            procedural("pkg://grunge_noise", 15, .3f, {})},
                           {b.blend(1, 25, .5f),
                            b.blend(8192, 25, .5f),
                            b.blend(128, 25, .25f),
                            b.blend(2, 0, .9f),
                            b.blend(8388608, 25, .6f)});
  const auto outer = group({inner},
                           {b.blend(1, 25, .2f),
                            b.blend(8192, 0),
                            b.blend(128, 25, .8f),
                            b.blend(2, 25, .3f),
                            b.blend(8388608, 25, .4f)});
  const auto mask_fill = b.fill({b.uniform(0, {.4f, .4f, .4f}, .5f)}, {b.blend(0, 4, .6f)});
  const auto mask_inner = group({mask_fill}, {b.blend(UINT64_MAX, 25, .25f)});
  const auto mask_outer = group({b.fill({b.uniform(0, {.8f, .8f, .8f})}), mask_inner},
                                {b.blend(UINT64_MAX, 11, .5f)});
  const auto tint = b.object("DataTweakColor",
                             {{"identifier", b.text("color")}, {"value", b.rgb({.1f, .3f, .7f})}});
  const auto colored_proc = b.object(
      "DataProcedural",
      {{"urlToSbsRes", b.text("pkg://grunge_noise")}, {"tweaks", b.array({tint})}});
  const auto colored = b.object("DataSourceProcedural",
                                {{"channelTypes", b.integer(1)}, {"procedural", colored_proc}});
  const auto disabled = group({b.fill({colored})}, {b.blend(1, 0)});
  const auto target = b.layer(
      "nested mixes",
      {fill(.2f, 1, 2, 1), outer, fill(.1f, 1, 4, .4f), disabled},
      {mask_outer, b.fill({b.uniform(0, {.1f, .1f, .1f})}, {b.blend(0, 10, .2f)})},
      .2f);
  auto options = color_policy();
  options.native_actions = true;
  options.uniform_rgb_to_scene_linear = [](std::array<float, 3> value) { return value; };
  const auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {target}))), options);
  const auto &out = named(prepared, "nested mixes");
  require(out.effects.size() == 31 && out.effects.back().kind == ContentEffectKind::Generator &&
              out.effects.back().opacity == 0,
          "Nested group lost child actions or disabled procedural tint escaped its source");
  const int channels[]{0, 1, 2, 4, 5};
  const float inherited[]{.1f, 0, .2f, 0, .24f};
  const ContentEffectKind kinds[]{ContentEffectKind::Fill,
                                  ContentEffectKind::Levels,
                                  ContentEffectKind::Filter,
                                  ContentEffectKind::Generator};
  const LayerBlend modes[]{
      LayerBlend::Overlay, LayerBlend::Multiply, LayerBlend::LinearDodge, LayerBlend::LinearLight};
  const float local[]{.56f, .6f, .4f, .3f};
  for (int block = 0; block < 4; ++block)
    for (int c = 0; c < 5; ++c) {
      const auto &e = out.effects[5 + block * 5 + c];
      require(e.kind == kinds[block] && e.channel == channels[c],
              "Grouped action kind/channel order changed");
      const auto mode = block == 2 && c == 3 ? LayerBlend::SignedAddition : modes[block];
      require(
          e.blending == mode && std::abs(e.opacity - inherited[c] * local[block]) < 1e-6f,
          "Nested group reused Color opacity, lost per-channel inheritance or ignored Disabled");
    }
  for (int c = 0; c < 5; ++c)
    require(out.effects[25 + c].kind == ContentEffectKind::Fill &&
                out.effects[25 + c].channel == channels[c] && out.effects[25 + c].opacity == .4f,
            "Inherited group opacity leaked to the following sibling Fill");
  const auto decoded = read_effects(effects_json(out.effects));
  for (std::size_t i = 0; i < out.effects.size(); ++i)
    require(decoded[i].channel == out.effects[i].channel &&
                decoded[i].blending == out.effects[i].blending &&
                decoded[i].opacity == out.effects[i].opacity,
            "Grouped action mix lost codec roundtrip");
  // Numerical point chain on opaque channels: initial Fill, Overlay source,
  // Multiply Levels, additive half-value HSL, LinearLight noise, sibling Fill.
  for (int index : {0, 1, 2, 4}) {
    const int c = channels[index];
    const float group_opacity = inherited[index];
    const auto noise = generated_mask(out.effects[20 + index], nullptr, 7.5 / 16, 4.5 / 16, 1001);
    require(noise.has_value(), "Grouped CPU grunge unavailable");
    const float filled = std::lerp(.2f, 2 * .2f * .8f, .56f * group_opacity);
    const float curved = std::lerp(filled, filled * std::sqrt(filled), .6f * group_opacity);
    const float filtered = std::lerp(curved, curved * 1.5f, .4f * group_opacity);
    const float generated = std::lerp(filtered, filtered + 2 * (*noise) - 1, .3f * group_opacity);
    const float expected = std::lerp(generated, generated * .1f, .4f);
    float coverage = 1;
    const auto actual = apply_content_effects(
        out.effects, c, {0, 0, 0}, coverage, 7, 4, 1001, 16, 16, ChannelPrecision::RGB32F);
    require(std::abs(actual[0] - expected) < 1e-6f,
            "Nested channel mix or following sibling differs from independent CPU arithmetic");
  }
  require(!out.channels[3] && !out.displacement.enabled,
          "Grouped scalar actions exposed Normal or displacement");
  require(out.mask->effects.size() == 2 && out.mask->effects[0].kind == MaskEffectKind::Group &&
              out.mask->effects[0].blending == LayerBlend::Overlay &&
              out.mask->effects[0].opacity == .5f,
          "All-channel scalar mask group lost authored mix");
  const auto &children = *out.mask->effects[0].group_effects;
  require(children.size() == 2 && children[1].blending == LayerBlend::Multiply &&
              std::abs(children[1].opacity - .075f) < 1e-6f,
          "Nested pass-through mask lost source/action/group opacity");
  const float mask_source = std::lerp(.8f, .8f * .4f, .075f);
  const float mask_expected = std::lerp(.2f, 2 * .2f * mask_source, .5f) + .1f * .2f;
  require(std::abs(out.mask->value(7, 4, 1001, 16, 16) - mask_expected) < 1e-6f,
          "Isolated scalar mask group or following sibling arithmetic changed");
  auto mask_copy = *out.mask;
  mask_copy.effects = read_mask_effects(mask_effects_json(out.mask->effects));
  require(std::abs(mask_copy.value(7, 4, 1001, 16, 16) - mask_expected) < 1e-6f,
          "Nested scalar mask codec changed mix behavior");
  require(has(prepared, "native-mask-all-channels-mix") && has(prepared, "nested-action-group"),
          "Mask sentinel interpretation or source isolation limitation was hidden");
  std::cout
      << "PASS nested group mixes: independent channel opacity, Disabled channels, ordered Fill/Levels/HSL/grunge, sibling scope, scalar all-channel mask groups, codec and independent CPU arithmetic\n";
}
void ordered_generator_filters()
{
  Builder b;
  const auto generator =
      [&](const char *uri, std::uint64_t channels, std::vector<s::NodeId> blends) {
        const auto proc = b.object("DataProcedural",
                                   {{"urlToSbsRes", b.text(uri)}, {"tweaks", b.array({})}});
        return b.object("DataActionFilterProcedural",
                        {{"channelTypes", b.integer(channels)},
                         {"enabled", b.boolean(true)},
                         {"procedural", proc},
                         {"perChannelBlending", b.array(std::move(blends))}});
      };
  const auto initial = [&] {
    return b.fill({b.uniform(1, {.2f, .3f, .4f}),
                   b.uniform(8192, {.6f, .6f, .6f}),
                   b.uniform(128, {.25f, .25f, .25f}),
                   b.uniform(2, {.1f, .1f, .1f}),
                   b.uniform(8388608, {.7f, .7f, .7f})});
  };
  const auto mask = [&] { return b.fill({b.uniform(0, {.6f, .6f, .6f})}); };
  const auto noise = generator("pkg://grunge_noise",
                               1 | 8192 | 2 | 8388608,
                               {b.blend(1, 11, .37f),
                                b.blend(8192, 4, .23f),
                                b.blend(2, 10, .44f),
                                b.blend(8388608, 15, .65f)});
  const auto rust = generator("pkg://rust_weathering", 128, {b.blend(128, 4, .31f)});
  const auto peeling = generator("pkg://mg_peeling", 1, {b.blend(1, 11, .22f)});
  const auto final = b.fill({b.uniform(1, {.4f, .2f, .1f}), b.uniform(128, {.9f, .9f, .9f})},
                            {b.blend(1, 10, .17f), b.blend(128, 4, .43f)});
  const auto target = b.layer(
      "generator actions", {initial(), noise, rust, peeling, final}, {mask()}, .8f);
  const auto reference = b.layer("base reference", {initial()}, {mask()}, .8f);
  const auto mask_target = b.layer(
      "mask generator",
      {initial()},
      {mask(), generator("pkg://grunge_noise", 0, {b.blend(0, 15, .29f)})},
      .8f);
  auto options = color_policy();
  options.native_actions = true;
  const auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {target, reference, mask_target}))),
      options);
  const auto &out = named(prepared, "generator actions"),
             &base = named(prepared, "base reference");
  require(out.effects.size() == 13, "Generator filter channel selection or source order lost");
  const int channels[]{0, 1, 4, 5, 2, 0};
  const LayerBlend modes[]{LayerBlend::Overlay,
                           LayerBlend::Multiply,
                           LayerBlend::SignedAddition,
                           LayerBlend::LinearLight,
                           LayerBlend::Multiply,
                           LayerBlend::Overlay};
  const float opacities[]{.37f, .23f, .44f, .65f, .31f, .22f};
  for (int i = 0; i < 6; ++i) {
    const auto &e = out.effects[5 + i];
    require(e.kind == ContentEffectKind::Generator && e.channel == channels[i] &&
                e.blending == modes[i] && e.opacity == opacities[i],
            "Generator filter lost per-channel blend/opacity or action position");
  }
  require(out.effects[11].kind == ContentEffectKind::Fill && out.effects[11].channel == 0 &&
              out.effects[12].channel == 2,
          "Subsequent Fill moved before generator filters");
  require(out.values.base_color == base.values.base_color &&
              out.values.roughness == base.values.roughness &&
              out.values.metallic == base.values.metallic,
          "Rust filter rewrote authored base material");
  require(out.mask->background == base.mask->background && out.mask->effects.size() == 1 &&
              out.mask->effects[0].kind == MaskEffectKind::Fill &&
              out.mask->effects[0].fill == base.mask->effects[0].fill &&
              out.mask->effects[0].opacity == base.mask->effects[0].opacity,
          "Content rust/peeling escaped its action into the layer mask");
  const auto saved = read_effects(effects_json(out.effects));
  for (int i = 0; i < 6; ++i)
    require(saved[5 + i].channel == channels[i] && saved[5 + i].blending == modes[i] &&
                saved[5 + i].opacity == opacities[i],
            "Generator action mix lost codec roundtrip");
  // Independent arithmetic for a noncommuting chain: Color Fill -> noise
  // Overlay -> peeling Overlay -> final LinearDodge. No texture/GPU context.
  MeshMaps maps;
  for (int role : {3, 4}) {
    MeshMap map;
    map.name = "CPU generator input";
    std::vector<float> rgba(32 * 32 * 4);
    for (int i = 0; i < 32 * 32; ++i) {
      rgba[i * 4] = role == 3 ? .8f : .75f;
      rgba[i * 4 + 3] = 1;
    }
    auto pixels = std::make_shared<MeshMapPixels>(32, std::move(rgba));
    map.tiles[1001] = {32, pixels->hash, pixels};
    maps[role] = std::move(map);
  }
  const auto n = generated_mask(out.effects[5], &maps, 7.5 / 16, 4.5 / 16, 1001),
             p = generated_mask(out.effects[10], &maps, 7.5 / 16, 4.5 / 16, 1001);
  require(n && p, "Native generator CPU source unavailable");
  const float first = std::lerp(.4f, blend_value(LayerBlend::Overlay, *n, .4f), .37f);
  const float second = std::lerp(first, blend_value(LayerBlend::Overlay, *p, first), .22f);
  const float expected = std::lerp(second, second + .8f, .17f);
  float coverage = 1;
  const auto value = apply_content_effects(out.effects,
                                           0,
                                           {0, 0, 0},
                                           coverage,
                                           7,
                                           4,
                                           1001,
                                           16,
                                           16,
                                           ChannelPrecision::RGB32F,
                                           nullptr,
                                           nullptr,
                                           &maps);
  require(std::abs(value[0] - expected) < 1e-6f,
          "Generator action evaluation lost noncommuting source order/mix");
  const auto &m = named(prepared, "mask generator");
  require(m.mask->effects.size() == 2 && m.mask->effects[1].kind == MaskEffectKind::Generator &&
              m.mask->effects[1].blending == LayerBlend::LinearLight &&
              m.mask->effects[1].opacity == .29f,
          "Scalar mask generator mix regressed");
  require(has(prepared, "native-procedural-approximation") &&
              has(prepared, "native-generator-filter-approximation"),
          "Multi-output generator approximation was not diagnosed");
  Builder ao;
  const auto proc = ao.object(
      "DataProcedural",
      {{"urlToSbsRes", ao.text("pkg://grunge_noise")}, {"tweaks", ao.array({})}});
  const auto action = ao.object("DataActionFilterProcedural",
                                {{"channelTypes", ao.integer(8388608)},
                                 {"procedural", proc},
                                 {"perChannelBlending", ao.array({ao.blend(8388608, 4, .4f)})}});
  const auto only = ao.layer("AO added later",
                             {ao.fill({ao.uniform(128, {.25f, .25f, .25f})}), action});
  const auto added = prepare_spsm_material(
      sem::inspect_archive(ao.archive(ao.folder("root", {only}))), options);
  const auto &a = named(added, "AO added later");
  require(
      added.material.set.ao_channel && a.ao_enabled && !a.channels[0] && !a.channels[3] &&
          a.effects.back().channel == 5,
      "Secondary AO generator did not enable the destination AO channel or enabled unrelated channels");
  std::cout
      << "PASS ordered generator filters: selected Color/Metal/Bump/AO, Overlay/Multiply/Add/LinearLight opacity, rust/peeling stay out of base/mask, codec and noncommuting CPU result\n";
}
void mixed_bitmap_actions(bool mixed_levels = false)
{
  Builder b;
  const auto identity = [](std::array<float, 3> rgb) { return rgb; };
  const auto bitmap = [&](std::uint64_t channel) {
    const auto data = b.object("DataBitmap",
                               {{"urlToBitmapRes", b.text("/test?version=fixture.image")}});
    return b.object(
        "DataSourceBitmap",
        {{"bitmap", data}, {"channelTypes", b.integer(channel)}, {"opacity", b.scalar(1)}});
  };
  const auto procedural = [&](std::uint64_t channel, const char *name) {
    const auto data = b.object("DataProcedural",
                               {{"urlToSbsRes", b.text(name)}, {"tweaks", b.array({})}});
    return b.object(
        "DataSourceProcedural",
        {{"procedural", data}, {"channelTypes", b.integer(channel)}, {"opacity", b.scalar(1)}});
  };
  const auto levels = [&](std::uint64_t channel) {
    const auto curve = b.object("DataLevels",
                                {{"clamp", b.integer(0)},
                                 {"gamma", b.rgb({2, 2, 2})},
                                 {"inputMinimum", b.rgb({0, 0, 0})},
                                 {"inputMaximum", b.rgb({1, 1, 1})},
                                 {"outputMinimum", b.rgb({0, 0, 0})},
                                 {"outputMaximum", b.rgb({1, 1, 1})}});
    return b.object(
        "DataActionFilterLevels",
        {{"channelTypes", b.integer(channel)},
         {"enabled", b.boolean(true)},
         {"levels", curve},
         {"perChannelBlending",
          b.array({b.blend(channel, mixed_levels ? 4 : 2, mixed_levels ? .37f : 1.f)})}});
  };
  const auto sharpen = [&](std::uint64_t channel) {
    const auto data = b.object(
        "DataProcedural", {{"urlToSbsRes", b.text("pkg://fx_sharpen")}, {"tweaks", b.array({})}});
    return b.object("DataActionFilterProcedural",
                    {{"channelTypes", b.integer(channel)},
                     {"enabled", b.boolean(true)},
                     {"procedural", data}});
  };
  const auto image = bitmap(1), mask_image = bitmap(0);
  const auto first = b.fill({image});
  const auto generated = b.fill({procedural(1, "pkg://grunge_map_test")}, {b.blend(1, 11, .22f)});
  const auto target = b.layer("mixed",
                              {first, generated, levels(1), sharpen(1)},
                              {b.fill({mask_image}),
                               b.fill({procedural(0, "pkg://mg_curvature")}, {b.blend(0, 4, .6f)}),
                               levels(0),
                               sharpen(0)},
                              0);
  auto options = SpsmImportOptions{};
  options.uniform_rgb_to_scene_linear = identity;
  auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {target}))), options);
  auto &out = prepared.material.set.layers.front();
  auto pixels = std::make_shared<FillTexture>();
  pixels->asset = "3df0f07b-9a6c-40de-b38b-85144399e3b2";
  pixels->hash = std::string(64, 'b');
  pixels->name = "Independent mixed bitmap fixture";
  pixels->width = pixels->height = 1;
  pixels->raw = true;
  pixels->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      1, FillTexture::Pixel{.3f, .3f, .3f, 1});
  out.fill.texture = pixels;
  out.fill.alpha_mask[0] = false;
  out.values.base_color = {1, 1, 1, 1};
  out.channels[0] = true;
  LevelsEffect boundary;
  boundary.kind = ContentEffectKind::LinearColorTransform;
  boundary.linear_color = std::make_shared<LinearColorTransform>();
  out.effects = {boundary};
  FillProjection mask_projection;
  mask_projection.texture = pixels;
  mask_projection.alpha_mask[0] = false;
  prepared.bitmap_mask_sources.emplace(mask_image, mask_projection);
  // Strict standalone/bitmap callers retain their original admission boundary;
  // native ordered lowering separately validates the same mix rows before opt-in.
  for (const auto &action : prepared.source->presets[0].actions)
    if (action.kind == sem::ActionKind::Levels) {
      bool strict = false;
      try {
        validate_spsm_levels_action(prepared.source->source().presets[0], action);
      }
      catch (const SpsmLevelsUnsupported &) {
        strict = true;
      }
      require(strict,
              "Strict Levels parser admitted per-channel mix without explicit native policy");
    }
  lower_spsm_native_actions(prepared, identity);
  if (out.effects.size() != 5) {
    std::cerr << "MIXED_DIAGNOSTIC effects=" << out.effects.size()
              << " mask=" << out.mask->effects.size()
              << " initial=" << prepared.initial_fill_actions.size() << '\n';
    for (const auto &effect : out.effects)
      std::cerr << " effect=" << int(effect.kind) << " channel=" << effect.channel
                << " name=" << effect.name << '\n';
    for (const auto &d : prepared.diagnostics)
      std::cerr << " diagnostic=" << d.code << " source=" << d.source << '\n';
  }
  require(out.effects.size() == 5 && out.effects[0].initialize_content &&
              out.effects[0].source_coverage &&
              out.effects[1].kind == ContentEffectKind::LinearColorTransform &&
              out.effects[2].kind == ContentEffectKind::Generator &&
              out.effects[3].kind == ContentEffectKind::Levels &&
              out.effects[4].kind == ContentEffectKind::Filter,
          "Bitmap content caused generator/Levels/filter omission or reordering");
  require(out.mask->effects.size() == 4 && out.mask->effects[0].kind == MaskEffectKind::Fill &&
              out.mask->effects[1].kind == MaskEffectKind::Generator &&
              out.mask->effects[2].kind == MaskEffectKind::Levels &&
              out.mask->effects[3].kind == MaskEffectKind::Filter,
          "Bitmap mask caused generator/Levels/filter omission or reordering");
  require(raw_linear_color_source(out) != nullptr &&
              out.mask->effects[0].mapping.projection.texture == pixels,
          "Mixed chains lost named color boundary or shared mask image");
  float coverage = 1;
  const auto value = apply_content_effects(
      out.effects, 0, {.3f, .3f, .3f}, coverage, 7, 4, 1001, 16, 16, ChannelPrecision::RGB32F);
  const auto generated_value = generated_mask(out.effects[2], nullptr, 7.5 / 16, 4.5 / 16, 1001);
  require(generated_value.has_value(), "Mixed native grunge unavailable");
  require(out.effects[2].surface_fallback && !out.effects[2].triplanar_grunge &&
              out.mask->effects[1].surface_fallback && !out.mask->effects[1].triplanar_grunge,
          "New native content/mask generator factory defaults lost");
  const float before = std::lerp(
      .3f, blend_value(LayerBlend::Overlay, *generated_value, .3f), .22f);
  const float expected = mixed_levels ? before * .63f + std::sqrt(before) * before * .37f :
                                        std::sqrt(before);
  require(std::abs(value[0] - expected) < 1e-6f,
          "Mixed bitmap/generator/Levels chain lost noncommuting evaluation order");
  if (mixed_levels) {
    require(out.effects[3].opacity == .37f && out.effects[3].blending == LayerBlend::Multiply,
            "Content Levels action mix dropped during lowering");
    require(out.mask->effects[2].opacity == .37f &&
                out.mask->effects[2].blending == LayerBlend::Multiply,
            "Mask Levels action mix dropped during lowering");
    const auto content = read_effects(effects_json(out.effects));
    const auto mask = read_mask_effects(mask_effects_json(out.mask->effects));
    require(content[3].opacity == .37f && content[3].blending == LayerBlend::Multiply &&
                mask[2].opacity == .37f && mask[2].blending == LayerBlend::Multiply,
            "Imported Levels mix lost on codec roundtrip");
  }
  std::cout
      << "PASS mixed bitmap actions: exact initial source id, shared bitmap mask, ordered native generator/Levels/filter, named color boundary, noncommuting result\n";
}
void internal_normal()
{
  Builder b;
  const auto uniform = b.layer("uniform Normal", {b.fill({b.uniform(4194304, {.7f, .4f, .94f})})});
  const auto bitmap = b.object("DataBitmap",
                               {{"urlToBitmapRes", b.text("/normal?version=fixture.image")}});
  const auto source = b.object("DataSourceBitmap",
                               {{"bitmap", bitmap}, {"channelTypes", b.integer(4194304)}});
  const auto image_layer = b.layer("bitmap Normal", {b.fill({source})});
  auto options = color_policy();
  auto prepared = prepare_spsm_material(
      sem::inspect_archive(b.archive(b.folder("root", {uniform, image_layer}))), options);
  auto pixels = std::make_shared<FillTexture>();
  pixels->width = pixels->height = 1;
  pixels->raw = true;
  pixels->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      1, FillTexture::Pixel{.7f, .4f, .94f, 1});
  auto &image = prepared.material.set.layers[1];
  fill_channel_texture(image.fill, 3) = pixels;
  image.channels[3] = true;
  lower_spsm_native_actions(prepared, options.uniform_rgb_to_scene_linear);
  const auto &constant = named(prepared, "uniform Normal");
  require(constant.channels[3] && constant.effects.size() == 1 &&
              constant.effects[0].channel == 3 &&
              constant.effects[0].fill_value == std::array<float, 3>{.7f, .4f, .94f},
          "Known uniform Normal data discarded or color transformed");
  const auto &bound = named(prepared, "bitmap Normal");
  require(bound.channels[3] && fill_channel_texture(bound.fill, 3) == pixels &&
              bound.effects.size() == 1 && bound.effects[0].source_coverage &&
              prepared.material.set.channels[3],
          "Known embedded Normal binding discarded or internally disabled");
  require(has(prepared, "native-internal-normal"), "Normal convention assumption not reported");
  const auto reopened = read_effects(effects_json(constant.effects));
  float coverage = 1;
  const auto value = apply_content_effects(
      reopened, 3, {.5, .5, 1}, coverage, 0, 0, 1001, 16, 16, ChannelPrecision::RGB32F);
  require(value == std::array<float, 3>{.7f, .4f, .94f},
          "Internal encoded Normal codec/evaluation lost RGB");
  Builder height;
  const auto layer = height.layer("height",
                                  {height.fill({height.uniform(2, {-.2f, -.2f, -.2f})})});
  auto height_options = color_policy();
  height_options.native_actions = true;
  auto imported = prepare_spsm_material(
      sem::inspect_archive(height.archive(height.folder("root", {layer}))), height_options);
  TextureSet legacy;
  legacy.bump_origin = 0;
  const auto adapted = spsm_material_for_destination(imported, legacy);
  require(std::abs(adapted.set.layers.front().effects.front().fill_value[0] + .2f) < 1e-6f &&
              imported.material.set.layers.front().effects.front().fill_value[0] > .29f,
          "Legacy Bump initial Fill adaptation lost neutral or modified source");
  std::cout
      << "PASS hidden internal Normal: uniform and bitmap retained, encoded RGB codec, explicit tangent-convention assumption\n";
}
void nested_optional_mesh_alias()
{
  Builder b;
  const auto bitmap = b.object("DataBitmap",
                               {{"urlToBitmapRes", b.text("resourcealias:AmbientOcclusion")}});
  const auto alias = b.object("DataSourceBitmap",
                              {{"bitmap", bitmap}, {"channelTypes", b.integer(0)}});
  const auto group = b.object("DataActionGroup",
                              {{"enabled", b.boolean(true)},
                               {"perChannelBlending", b.array({b.blend(0, 2)})},
                               {"subStack", b.stack("DataStackActions", {b.fill({alias})})}});
  const auto layer = b.layer(
      "nested optional AO", {b.fill({b.uniform(1, {.3f, .4f, .5f})})}, {group}, 0);
  auto options = color_policy();
  auto prepared = prepare_spsm_material(sem::inspect_archive(b.archive(b.folder("root", {layer}))),
                                        options);
  lower_spsm_native_actions(prepared, options.uniform_rgb_to_scene_linear);
  const auto &out = named(prepared, "nested optional AO");
  const auto &parent = out.mask->effects.front();
  require(parent.kind == MaskEffectKind::Group && parent.group_effects &&
              parent.group_effects->size() == 1,
          "Nested mesh alias lost isolated Group");
  const auto &generator = parent.group_effects->front();
  require(generator.kind == MaskEffectKind::Generator && generator.generator == 3 &&
              generator.optional_inputs,
          "Nested imported AO alias unexpectedly requires maps before applying material");
  require(out.visible && out.channels[0] && out.values.base_color[0] > .59f,
          "Pending mesh alias disabled or erased base Fill");
  require(has(prepared, "native-meshmap-alias"),
          "Provisional destination mesh-map alias not diagnosed");
  MeshGeneratorSettings builtin;
  builtin.generator = 3;
  require(!builtin.surface_fallback && !builtin.triplanar_grunge,
          "Legacy struct generator defaults changed");
  const auto legacy = read_mesh_generator(mesh_generator_json(builtin));
  require(!legacy.surface_fallback && !legacy.triplanar_grunge,
          "Legacy generator decode defaults changed");
  require(!builtin.optional_inputs, "Imported alias relaxed builtin generator policy");
  std::cout
      << "PASS nested optional AO alias: authored visibility/base Fill retained before bake, provisional mapping diagnosed, builtin policy strict\n";
}
void gpu_mask_group_contract()
{
  MaskEffect paint;
  paint.kind = MaskEffectKind::Paint;
  MaskEffect filter;
  filter.kind = MaskEffectKind::Filter;
  filter.filter.resource = 9;
  MaskEffect inner;
  inner.kind = MaskEffectKind::Group;
  inner.group_effects = std::make_shared<const std::vector<MaskEffect>>(
      std::vector{paint, filter});
  MaskEffect outer = inner;
  outer.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{inner});
  require(gpu_mask_effect_supported(outer), "Nested Group rejected by native GPU admission");
  MaskEffect unknown;
  unknown.kind = MaskEffectKind(999);
  inner.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{unknown});
  outer.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{inner});
  require(!gpu_mask_effect_supported(outer), "Unknown nested effect admitted to GPU");
  MaskEffect empty;
  empty.kind = MaskEffectKind::Group;
  empty.blending = LayerBlend::Multiply;
  LayerMask mask;
  mask.background = .6f;
  mask.effects = {empty};
  require(gpu_mask_effect_supported(empty) && mask.value(0, 0) == 0,
          "Null Group must resolve empty child against zero");
  const auto roundtrip = read_mask_effects(mask_effects_json(mask.effects));
  mask.effects = roundtrip;
  require(mask.value(0, 0) == 0, "Empty Group codec changes zero-child semantics");
  std::cout
      << "PASS native mask Group admission, strict unknown-child rejection, empty/null child codec semantics\n";
}
void nested_bitmap_resources()
{
  auto image = std::make_shared<FillTexture>();
  image->asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  image->hash = std::string(64, 'd');
  image->name = "Nested bitmap fixture";
  image->width = image->height = 2;
  image->raw = true;
  image->input_space = "Non-Color/raw";
  image->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      std::vector<FillTexture::Pixel>{
          {.25, .25, .25, 1}, {.81, .81, .81, 1}, {.25, .25, .25, 1}, {.81, .81, .81, 1}});
  MaskEffect bitmap;
  bitmap.kind = MaskEffectKind::Fill;
  bitmap.fill = 1;
  bitmap.mapping.projection.texture = image;
  bitmap.mapping.projection.alpha_mask[0] = false;
  MaskEffect inner;
  inner.kind = MaskEffectKind::Group;
  inner.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{bitmap});
  MaskEffect outer = inner;
  outer.id = QUuid::createUuid();
  outer.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{inner});
  LayerMask original;
  original.effects = {outer};
  MaskPresetTextures resources(original);
  require(resources.resources.size() == 1 && resources.bindings.size() == 1,
          "Nested bitmap absent from resource bank");
  QByteArray bytes;
  QDataStream writer(&bytes, QIODevice::WriteOnly);
  writer.setByteOrder(QDataStream::LittleEndian);
  writer.setFloatingPointPrecision(QDataStream::SinglePrecision);
  std::uint64_t decoded = 0;
  resources.write(writer, decoded, 1024 * 1024);
  LayerMask reopened;
  reopened.effects = read_mask_effects(mask_effects_json(original.effects));
  QDataStream reader(bytes);
  reader.setByteOrder(QDataStream::LittleEndian);
  reader.setFloatingPointPrecision(QDataStream::SinglePrecision);
  decoded = 0;
  MaskPresetTextures::read(reader,
                           reopened,
                           QJsonObject{{"texture_resources", resources.descriptors},
                                       {"texture_bindings", resources.bindings}},
                           decoded,
                           1024 * 1024);
  const MaskEffect *resolved = nullptr;
  for (const auto &effect : mask_effects_recursive(std::as_const(reopened.effects)))
    if (effect.kind == MaskEffectKind::Fill)
      resolved = &effect;
  require(resolved && resolved->mapping.projection.texture &&
              resolved->mapping.projection.texture->pixels &&
              *resolved->mapping.projection.texture->pixels == *image->pixels,
          "Nested bitmap resource reopen unresolved");
  require(std::abs(reopened.value(4, 4, 1001, 16, 16) - original.value(4, 4, 1001, 16, 16)) <
              1e-6f,
          "Nested bitmap behavior changed on reopen");
  auto edited = reopened;
  for (auto &effect : mask_effects_recursive(edited.effects))
    if (effect.kind == MaskEffectKind::Fill)
      effect.fill = .2f;
  require(resolved->fill == 1 &&
              original.effects[0].group_effects->front().group_effects->front().fill == 1,
          "Editing nested bitmap changed shared source preset");
  std::cout
      << "PASS nested bitmap resources: two Group levels, bank/codec pixels and mask evaluation, immutable edit detachment\n";
}
void spatial_oracle_scope()
{
  TextureSet set;
  require(!spatial_oracle_unavailable(set), "Point-only source incorrectly rejected");
  MaskEffect filter;
  filter.kind = MaskEffectKind::Filter;
  filter.filter.resource = 9;
  MaskEffect group;
  group.kind = MaskEffectKind::Group;
  group.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{filter});
  set.layers.front().visible = false;
  set.layers.front().mask = LayerMask{};
  set.layers.front().mask->effects = {group};
  require(spatial_oracle_unavailable(set).has_value(),
          "Nested hidden mask anchor producer silently accepted by whole-stack oracle");
  set.layers.front().mask.reset();
  LevelsEffect content;
  content.kind = ContentEffectKind::Filter;
  content.filter.resource = 10;
  set.layers.front().effects = {content};
  require(spatial_oracle_unavailable(set).has_value(),
          "Content spatial producer silently accepted by whole-stack oracle");
  set.layers.front().effects.front().enabled = false;
  require(!spatial_oracle_unavailable(set), "Disabled point-only source rejected");
  std::cout
      << "PASS whole-material CPU oracle scope: nested and hidden spatial producers unavailable, point-only remains eligible\n";
}
// A standalone mask generator is a scalar action stack even where an authored
// source selects RGB bit 1. This is not a widened material-channel policy.
void standalone_smart_mask()
{
  const auto smart_archive = [](Builder &b, std::vector<s::NodeId> actions) {
    b.p.root = b.object("DataSmartMask",
                        {{"actionsStack", b.stack("DataStackActions", std::move(actions))}});
    b.p.dataset = "/preset.bin";
    b.p.encoded = {17, 43};
    s::Archive archive;
    archive.source = "synthetic.spmsk";
    archive.presets.push_back(std::move(b.p));
    return archive;
  };
  auto options = color_policy();
  options.native_actions = true;
  Builder b;
  const auto first = b.fill({b.uniform(0, {.8f, .8f, .8f})}, {b.blend(0, 2, .9f)});
  const auto second = b.fill({b.uniform(0, {.2f, .2f, .2f})}, {b.blend(0, 29)});
  const auto third = b.fill({b.uniform(1, {.4f, .4f, .4f})}, {b.blend(0, 9001, .35f)});
  auto out = prepare_spsm_material(sem::inspect_archive(smart_archive(b, {first, second, third})),
                                   options);
  require(out.material.set.layers.size() == 1, "Mask generator has no unique typed owner");
  const auto &mask = *out.material.set.layers.front().mask;
  require(mask.effects.size() == 3 && mask.effects[0].fill == .8f &&
              mask.effects[1].blending == LayerBlend::Subtract && mask.effects[2].fill == .4f &&
              mask.effects[2].opacity == .35f,
          "Standalone scalar/RGB inputs lost order or opacity");
  require(mask.background == 1 && has(out, "smart-mask-background-policy"),
          "Standalone application initial white policy missing");
  require(has(out, "native-smart-mask-source-channel") && has(out, "native-action-blend"),
          "Unknown standalone blend/RGB interpretation silently claimed supported");
  require(std::abs(mask.value(0, 0) - std::lerp(std::lerp(1.f, .8f, .9f) - .2f, .4f, .35f)) <
              1e-6f,
          "Standalone noncommuting blend contract changed");
  Builder editor;
  const auto tweak = [&](const char *name, float value) {
    return editor.object("DataTweakFloat",
                         {{"identifier", editor.text(name)}, {"value", editor.scalar(value)}});
  };
  const auto seed = tweak("$randomseed", 23834), unmapped = tweak("curv_smooth", .57f);
  const auto custom = editor.object("DataProcedural",
                                    {{"urlToSbsRes", editor.text("pkg://water_drips/water_drips")},
                                     {"tweaks", editor.array({seed})}});
  const auto custom_source = editor.object(
      "DataSourceProcedural", {{"channelTypes", editor.integer(1)}, {"procedural", custom}});
  const auto input = editor.object(
      "DataProceduralInputsSource",
      {{"inputs",
        editor.array({editor.object(
            "DataProceduralInput",
            {{"identifier", editor.text("texture")}, {"channelType", editor.integer(1)}})})},
       {"source", custom_source}});
  const auto proc = editor.object(
      "DataProcedural",
      {{"urlToSbsRes", editor.text("pkg://mg_mask_editor/mg_mask_editor")},
       {"tweaks",
        editor.array({tweak("global_balance", .71f),
                      tweak("global_contrast", .43f),
                      tweak("global_invert", 1),
                      unmapped})},
       {"sourcesInputs", editor.array({input})}});
  const auto source = editor.object("DataSourceProcedural",
                                    {{"channelTypes", editor.integer(0)}, {"procedural", proc}});
  const auto warp = editor.object(
      "DataProcedural",
      {{"urlToSbsRes", editor.text("/warp/warp?version=fixture.opaque")},
       {"tweaks",
        editor.array({tweak("intensity", 10),
                      tweak("intensity_divider", 3),
                      tweak("scale", 1),
                      tweak("angle", 0)})}});
  const auto action = editor.object(
      "DataActionFilterProcedural",
      {{"procedural", warp},
       {"enabled", editor.boolean(true)},
       {"perChannelBlending", editor.array({editor.blend(0, 2, .6f)})}});
  auto result = prepare_spsm_material(
      sem::inspect_archive(smart_archive(editor, {editor.fill({source}), action})), options);
  const auto &effects = result.material.set.layers.front().mask->effects;
  require(effects.size() == 2 && effects[0].kind == MaskEffectKind::Generator &&
              effects[1].kind == MaskEffectKind::Filter && effects[1].filter.resource == 9,
          "Actual /warp/warp family lost ordered native approximation");
  require(std::abs(effects[0].bias - (.71f - .5f)) < 1e-6f &&
              std::abs(effects[0].contrast - (1 + 7 * .43f)) < 1e-6f && effects[0].invert,
          "Global editor balance/contrast/invert lost");
  require(std::abs(effects[1].filter.value_scale - 10.f / 3) < 1e-6f && effects[1].opacity == .6f,
          "Warp parameter interpretation or opacity lost");
  const auto exact = [&](s::NodeId node, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const auto &d) {
      return d.source == node && d.code == code && !result.diagnostic_path(d).empty();
    });
  };
  require(exact(unmapped, "native-procedural-parameter-omitted") &&
              exact(input, "native-generator-input-omitted") &&
              exact(seed, "native-generator-input-parameter-omitted"),
          "Unknown typed custom controls lack precise retained-source diagnostics");
  std::cout
      << "PASS DataSmartMask: scalar source family, ordered Normal/Subtract/unknown-blend honesty, opacity, editor aliases, per-input/parameter omissions, /warp/warp native approximation\n";
}
void smart_mask_source_mixes()
{
  for (const char *package : {"water_drips/water_drips",
                              "mask_builder_drips/mask_builder_drips",
                              "mg_mask_editor/mg_mask_editor"})
  {
    Builder b;
    const auto proc = b.object(
        "DataProcedural",
        {{"urlToSbsRes", b.text(std::string("pkg://") + package)}, {"tweaks", b.array({})}});
    const auto source = b.object(
        "DataSourceProcedural",
        {{"channelTypes", b.integer(0)}, {"procedural", proc}, {"opacity", b.scalar(.6f)}});
    const auto tweak = b.object("DataTweakFloat",
                                {{"identifier", b.text("lightness")}, {"value", b.scalar(.9f)}});
    const auto filter = b.object("DataProcedural",
                                 {{"urlToSbsRes", b.text("pkg://hsl_perceptive/hsl_perceptive")},
                                  {"tweaks", b.array({tweak})}});
    const auto last = b.object("DataActionFilterProcedural",
                               {{"procedural", filter},
                                {"enabled", b.boolean(true)},
                                {"perChannelBlending", b.array({b.blend(0, 4, .5f)})}});
    b.p.root = b.object("DataSmartMask",
                        {{"actionsStack",
                          b.stack("DataStackActions",
                                  {b.fill({b.uniform(0, {.2f, .2f, .2f})}),
                                   b.fill({source}, {b.blend(0, 11, .4f)}),
                                   last})}});
    b.p.dataset = "/preset.bin";
    b.p.encoded = {17, 43};
    s::Archive archive;
    archive.source = "ordered-source.spmsk";
    archive.presets.push_back(std::move(b.p));
    auto options = color_policy();
    options.native_actions = true;
    const auto prepared = prepare_spsm_material(sem::inspect_archive(std::move(archive)), options);
    const auto &mask = *prepared.material.set.layers.front().mask;
    require(mask.effects.size() == 3, "CC0 approximation escaped its source action position");
    const auto &effect = mask.effects[1];
    require(effect.blending == LayerBlend::Overlay && std::abs(effect.opacity - .24f) < 1e-6f,
            "CC0 grunge replaced authored blend/source opacity");
    require(mask.effects.back().kind == MaskEffectKind::Filter &&
                mask.effects.back().blending == LayerBlend::Multiply &&
                mask.effects.back().opacity == .5f,
            "CC0 grunge moved after subsequent authored filter");
    require(effect.kind == MaskEffectKind::Generator && effect.response != 0,
            "Ordered procedural source lost its live parameters");
    TextureSet fixture;
    smart_mask_probe::profile(fixture, 8);
    MaskCompareInputs context;
    context.projection = fixture.projection_geometry.get();
    LayerMask source_value;
    source_value.background = 0;
    source_value.effects = {effect};
    source_value.effects[0].blending = LayerBlend::Normal;
    source_value.effects[0].opacity = 1;
    const float v = source_value.value(4, 4, 1001, 16, 16, nullptr, &fixture.mesh_maps, &context);
    const float mixed = std::lerp(.2f, blend_value(LayerBlend::Overlay, v, .2f), .24f);
    const float filtered = filter_value(mask.effects.back().filter, mixed);
    const float expected = std::clamp(
        std::lerp(mixed, blend_value(LayerBlend::Multiply, filtered, mixed), .5f), 0.f, 1.f);
    require(std::abs(mask.value(4, 4, 1001, 16, 16, nullptr, &fixture.mesh_maps, &context) -
                     expected) < 1e-6f,
            "CC0 source blend or following filter applied in wrong order");
    const auto roundtrip = read_mask_effects(mask_effects_json(mask.effects));
    require(roundtrip.size() == 3 && roundtrip[1].blending == LayerBlend::Overlay &&
                roundtrip[1].opacity == effect.opacity &&
                roundtrip.back().kind == MaskEffectKind::Filter,
            "Ordered source mix lost during codec");
  }
  std::cout
      << "PASS SPMSK CC0 grunge source mixes: source/action opacity once, authored Overlay/Multiply, filters after source, nested source detail and codec\n";
}
void smart_mask_generator_controls()
{
  const auto lower = [](const char *package,
                        std::initializer_list<std::pair<const char *, float>> controls) {
    Builder b;
    std::vector<s::NodeId> tweaks;
    for (const auto &[name, value] : controls)
      tweaks.push_back(
          b.object("DataTweakFloat", {{"identifier", b.text(name)}, {"value", b.scalar(value)}}));
    const auto proc = b.object("DataProcedural",
                               {{"urlToSbsRes", b.text(std::string("pkg://") + package)},
                                {"tweaks", b.array(std::move(tweaks))}});
    const auto source = b.object("DataSourceProcedural",
                                 {{"channelTypes", b.integer(0)}, {"procedural", proc}});
    auto options = color_policy();
    options.native_actions = true;
    return prepare_spsm_material(sem::inspect_archive(b.archive(b.folder(
                                     "root", {b.layer("controls", {}, {b.fill({source})})}))),
                                 options);
  };
  for (float curvature : {0.f, .8f}) {
    const auto p = lower("mg_mask_editor/mg_mask_editor",
                         {{"invert_ao", 1}, {"ao", 1}, {"curv", curvature}, {"tex", .5f}});
    require(named(p, "controls").mask->effects.front().generator == 9,
            "Inverted AO editor lost cavity support");
  }
  const auto noise = lower("mg_mask_editor/mg_mask_editor",
                           {{"invert_ao", 0}, {"curv", 0}, {"tex", .5f}});
  require(named(noise, "controls").mask->effects.front().generator == 7,
          "Texture-only editor no longer lowers to grunge");
  const auto modern = lower("mg_mask_editor/mg_mask_editor",
                            {{"ao_invert", 1},
                             {"ao_opacity", .3f},
                             {"curvature_opacity", 1},
                             {"texture_opacity", .7f},
                             {"texture_scale", 2},
                             {"texture_triplanar", 1},
                             {"ws_opacity", .2f},
                             {"position_opacity", .4f},
                             {"thickness_opacity", .55f}});
  const auto &e = named(modern, "controls").mask->effects.front();
  require(e.generator == 9 && e.ao_influence == .3f && e.curvature_influence == 1 &&
              e.grunge_amount == .7f && e.grunge_scale == 2 && e.triplanar_grunge &&
              e.normal_influence == .2f && e.position_influence == .4f &&
              e.thickness_influence == .55f,
          "Modern editor aliases lost their authored values");
  require(!has(modern, "native-procedural-parameter-omitted"),
          "Consumed editor aliases still reported as omitted");
  const auto rust = lower("mg_driping_rust/mg_driping_rust", {{"Rust_Contrast", .35f}});
  const auto &drips = named(rust, "controls").mask->effects.front();
  require(drips.generator == 9 && drips.response == 2 && drips.triplanar_grunge &&
              drips.grunge_pattern == 3,
          "Rust Drips lost gravity response");
  std::cout
      << "PASS mask generator controls: AO-only cavity support, modern editor aliases, Rust Drips gravity response\n";
}
void smart_mask_files(const std::filesystem::path &directory)
{
  const char *names[] = {"Rust",
                         "Cavity Rust",
                         "Edge Rust",
                         "Surface Rust",
                         "Edge Damage",
                         "Edges Scratched",
                         "Edges Strong",
                         "Dirt Cavities",
                         "Dirt Leak Dry",
                         "Moss From Top",
                         "Moss",
                         "Subtle Scratches",
                         "Water Drips",
                         "Dust Occlusion",
                         "Dirt Dry",
                         "Dirt Ground",
                         "Dust Dirty",
                         "Dust Soft",
                         "Rust Drips",
                         "Paint Old",
                         "Sand Cavities",
                         "Stains Surface",
                         "Occlusion Strong"};
  for (const auto name : names) {
    const auto path = directory / (std::string(name) + ".spmsk");
    auto options = color_policy();
    options.native_actions = true;
    auto p = prepare_spsm_material(path, options);
    require(p.material.set.layers.size() == 1 && p.material.set.layers.front().visible &&
                p.material.set.layers.front().mask,
            "Standalone mask owner missing/disabled");
    const auto &effects = p.material.set.layers.front().mask->effects;
    const auto all_effects = mask_effects_recursive(std::as_const(effects));
    const auto bitmap = [&] {
      for (const auto &e : all_effects)
        if (e.kind == MaskEffectKind::Fill && e.mapping.projection.mode == 1 &&
            e.mapping.projection.texture && e.mapping.projection.texture->width == 512)
          return true;
      return false;
    };
    require(!effects.empty() && p.partial && !p.appearance_verified,
            "Standalone source falsely equivalent or missing ordered actions");
    if (std::string_view(name) == "Edge Rust") {
      require(effects.size() == 3 && effects[0].blending == LayerBlend::Normal &&
                  effects[1].blending == LayerBlend::Subtract &&
                  effects[2].blending == LayerBlend::VividLight,
              "Edge Rust third source or original blend order silently dropped");
    }
    if (std::string_view(name) == "Water Drips") {
      require(std::any_of(effects.begin(),
                          effects.end(),
                          [](const auto &e) {
                            return e.kind == MaskEffectKind::Filter && e.filter.resource == 9;
                          }) &&
                  has(p, "native-generator-input-omitted"),
              "Water Drips filter/triplanar grunge bitmap vanished");
    }
    // Source semantics must be invariant under file naming. This fails for any
    // preset-name look adjustment, while retaining ordered authored controls.
    QTemporaryDir renamed;
    require(renamed.isValid(), "Cannot create renamed fixture");
    const auto alias = std::filesystem::path(renamed.path().toStdString()) / "anonymous.spmsk";
    std::filesystem::copy_file(path, alias);
    const auto same = prepare_spsm_material(alias, options);
    const auto &renamed_effects = same.material.set.layers.front().mask->effects;
    require(effects.size() == renamed_effects.size(), "Preset name changes effect count");
    for (std::size_t i = 0; i < effects.size(); ++i) {
      require(effects[i].kind == renamed_effects[i].kind &&
                  effects[i].blending == renamed_effects[i].blending &&
                  effects[i].opacity == renamed_effects[i].opacity,
              "Preset name changes effect semantics");
      if (effects[i].kind == MaskEffectKind::Generator)
        require(mesh_generator_json(effects[i]) == mesh_generator_json(renamed_effects[i]),
                "Preset name changes generator controls");
    }
    for (const auto &d : p.diagnostics)
      require(!p.diagnostic_path(d).empty(), "Mask generator diagnostic source path missing");
    std::cout << "PASS " << path.filename().string() << " effects=" << effects.size()
              << " diagnostics=" << p.diagnostics.size()
              << " native approximation; no graph execution/equivalence claim\n";
  }
}
void fixtures(const std::filesystem::path &directory)
{
  const char *names[] = {"dirty-plastic-robot.spsm",
                         "shd-hard-rust.spsm",
                         "sratched-metal-pealing-paint-dirt.spsm",
                         "used_varnish_wood.spsm",
                         "worn leather chest.spsm"};
  const std::size_t counts[] = {5, 4, 7, 26, 10};
  for (std::size_t fi = 0; fi < std::size(names); ++fi) {
    if (!std::filesystem::exists(directory / names[fi]))
      continue;
    auto options = color_policy();
    options.native_actions = true;
    auto p = prepare_spsm_material(directory / names[fi], options);
    hierarchy(p);
    require(p.material.set.layers.size() == counts[fi] && p.layers.size() == counts[fi],
            "fixture hierarchy loss");
    require(p.partial && !p.appearance_verified && has(p, "procedural-unimplemented"),
            "fixture falsely complete/equivalent");
    std::uint64_t encoded = 0;
    for (const auto &preset : p.source->source().presets)
      encoded += preset.encoded.size();
    require(p.source->source().payload_bytes_read == encoded,
            "lowering read image/package payload");
    const auto pi = *p.source->primary_preset;
    require(p.actions.size() == p.source->presets[pi].actions.size(),
            "source actions not exhaustively lowered/marked skipped");
    std::size_t visible = 0;
    for (const auto &l : p.material.set.layers)
      visible += l.visible && l.kind == LayerKind::Fill;
    require(visible > 0, "partial file has no useful native fill contribution");
    for (const auto &d : p.diagnostics) {
      require(d.preset < p.source->presets.size(), "diagnostic stream lost");
      require(p.diagnostic_path(d).starts_with('$'), "fixture diagnostic path lost");
    }
    if (fi == 0) {
      require(p.source->presets.size() == 3 && has(p, "alternate-preset-stream"),
              "alternate preset streams lost");
      require(
          p.material.set.layers[0].name == "Base" && p.material.set.layers[1].name == "Splats" &&
              p.material.set.layers[2].name == "Dirt" && p.material.set.layers[3].name == "Edges",
          "observed source order reversed");
      require(named(p, "Base").channels[4] && named(p, "Base").values.bump == .5f,
              "actual legacy zero Bump did not become neutral .5");
      require(named(p, "Dirt").visible && named(p, "Dirt").channels[0],
              "multiple fills lost authored visibility/base contribution");
    }
    const auto &arena = p.source->source().presets[pi];
    const auto field = [&](s::NodeId id, std::string_view key) -> std::optional<s::NodeId> {
      if (const auto object = std::get_if<s::Object>(&arena.nodes.at(id).value))
        for (const auto &f : object->fields)
          if (f.name == key)
            return f.value;
      return {};
    };
    for (const auto &action : p.source->presets[pi].actions)
      if (action.kind == sem::ActionKind::ProceduralFilter) {
        const auto proc = field(action.source, "procedural"),
                   uri = proc ? field(*proc, "urlToSbsRes") : std::optional<s::NodeId>{};
        if (!uri)
          continue;
        const auto name = std::get_if<std::string>(&arena.nodes.at(*uri).value);
        if (!name)
          continue;
        if (name->find("hsl_perceptive") != name->npos || name->find("fx_sharpen") != name->npos ||
            name->find("blur") != name->npos || name->find("fx_warp") != name->npos)
        {
          const auto binding = std::find_if(
              p.actions.begin(), p.actions.end(), [&](const auto &b) {
                return b.source == action.source;
              });
          require(binding != p.actions.end() && binding->lowered,
                  "Recognized real-file filter action was dropped");
        }
      }
    const auto metadata = s::read_preview_metadata(directory / names[fi]);
    require(metadata && !spsm_preview_detail::encoded_preview(*metadata).isEmpty(),
            "Real SPSM embedded shaderball preview missing");
    std::cout << "PASS " << names[fi] << " native_layers=" << counts[fi]
              << " visible_fill=" << visible << " actions=" << p.actions.size()
              << " diagnostics=" << p.diagnostics.size() << " metadata_only_bytes=" << encoded
              << '\n';
  }
}
}  // namespace
int main(int argc, char **argv)
{
  try {
    synthetic();
    native_semantics();
    advanced_ordered_actions();
    levels_channel_isolation();
    mask_levels_sentinels();
    mask_levels_admission();
    procedural_source_color();
    procedural_color_controls();
    grouped_action_mixes();
    ordered_generator_filters();
    mixed_bitmap_actions();
    mixed_bitmap_actions(true);
    internal_normal();
    nested_bitmap_resources();
    spatial_oracle_scope();
    gpu_mask_group_contract();
    nested_optional_mesh_alias();
    standalone_smart_mask();
    smart_mask_source_mixes();
    smart_mask_generator_controls();
    if (argc > 1)
      fixtures(argv[1]);
    if (argc > 2)
      smart_mask_files(argv[2]);
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SPSM native lowering failed: " << e.what() << '\n';
    return 1;
  }
}
