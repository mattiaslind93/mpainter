// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmMaterialImport.hh"
#include <algorithm>
#include <cmath>

namespace bts {
// Destination interpretation choices, never source enum ordinals. The explicit
// NativeScalarLevels policy uses reciprocal gamma, signed native intermediates
// and source clamp 0=off / 1=final [0,1]; source equivalence remains unverified.
enum class SpsmLevelsPolicy { Unknown, NativeScalarLevels };
enum class SpsmScalarDomain { Roughness128, MaskAuxiliary0 };
struct SpsmLevelsUnsupported {
  spsm::NodeId source;
};
namespace spsm_levels_detail {
class Parser {
  const spsm::Preset &p;
  bool ordered_mix_ = false;

 public:
  explicit Parser(const spsm::Preset &preset, bool validated_ordered_mix = false)
      : p(preset), ordered_mix_(validated_ordered_mix)
  {
  }
  const spsm::Node &node(spsm::NodeId id) const
  {
    return p.nodes.at(id);
  }
  const spsm::Object &object(spsm::NodeId id) const
  {
    auto o = std::get_if<spsm::Object>(&node(id).value);
    if (!o)
      throw SpsmLevelsUnsupported{id};
    return *o;
  }
  std::optional<spsm::NodeId> field(spsm::NodeId id, std::string_view key) const
  {
    std::optional<spsm::NodeId> found;
    for (const auto &f : object(id).fields)
      if (f.name == key) {
        if (found)
          throw spsm::Error("Duplicate SPSM Levels source field");
        found = f.value;
      }
    return found;
  }
  spsm::NodeId required(spsm::NodeId id, std::string_view key) const
  {
    auto f = field(id, key);
    if (!f)
      throw SpsmLevelsUnsupported{id};
    return *f;
  }
  void fields(spsm::NodeId id, std::initializer_list<std::string_view> allowed) const
  {
    const auto &all = object(id).fields;
    for (std::size_t i = 0; i < all.size(); ++i) {
      for (std::size_t j = 0; j < i; ++j)
        if (all[i].name == all[j].name)
          throw spsm::Error("Duplicate SPSM Levels source field");
      if (std::find(allowed.begin(), allowed.end(), all[i].name) == allowed.end())
        throw SpsmLevelsUnsupported{all[i].value};
    }
  }
  std::uint64_t bits(spsm::NodeId id) const
  {
    const auto &v = node(id).value;
    if (auto n = std::get_if<std::uint64_t>(&v))
      return *n;
    if (auto n = std::get_if<std::int64_t>(&v); n && *n >= 0)
      return *n;
    if (auto n = std::get_if<spsm::IntValues>(&v); n && n->count == 1 && n->values[0] >= 0)
      return n->values[0];
    throw SpsmLevelsUnsupported{id};
  }
  float scalar(spsm::NodeId id) const
  {
    auto v = std::get_if<spsm::FloatValues>(&node(id).value);
    if (!v || v->count != 3 || !std::isfinite(v->values[0]) || v->values[0] != v->values[1] ||
        v->values[1] != v->values[2])
      throw SpsmLevelsUnsupported{id};
    return v->values[0];
  }
  std::array<float, 3> rgb(spsm::NodeId id) const
  {
    const auto *v = std::get_if<spsm::FloatValues>(&node(id).value);
    if (!v || v->count != 3)
      throw SpsmLevelsUnsupported{id};
    std::array<float, 3> result;
    for (int c = 0; c < 3; ++c) {
      result[c] = v->values[c];
      if (!std::isfinite(result[c]))
        throw SpsmLevelsUnsupported{id};
    }
    return result;
  }
  spsm::NodeId action(const spsm::semantic::Action &a) const
  {
    if (a.kind != spsm::semantic::ActionKind::Levels ||
        object(a.source).class_name != "DataActionFilterLevels")
      throw SpsmLevelsUnsupported{a.source};
    fields(a.source,
           {"channelTypes",
            "enabled",
            "label",
            "levels",
            "selection",
            "tags",
            "uid",
            "perChannelBlending"});
    // Only the native ordered lowerer may opt in, after validating the mix
    // metadata itself. Verified standalone/bitmap curve admission stays strict.
    if (!ordered_mix_ && field(a.source, "perChannelBlending"))
      throw SpsmLevelsUnsupported{a.source};
    if (auto f = field(a.source, "enabled"); f && !std::holds_alternative<bool>(node(*f).value))
      throw SpsmLevelsUnsupported{*f};
    if (auto f = field(a.source, "selection")) {
      auto blob = std::get_if<spsm::Blob>(&node(*f).value);
      if (!blob || blob->size)
        throw SpsmLevelsUnsupported{*f};
    }
    if (auto f = field(a.source, "tags"); f && bits(*f) != 0)
      throw SpsmLevelsUnsupported{*f};
    if (!a.children.empty() || !a.sources.empty() || (!ordered_mix_ && !a.blending.empty()))
      throw SpsmLevelsUnsupported{a.source};
    bits(required(a.source, "channelTypes"));
    const auto id = required(a.source, "levels");
    if (object(id).class_name != "DataLevels")
      throw SpsmLevelsUnsupported{id};
    fields(id,
           {"clamp",
            "gamma",
            "inputMaximum",
            "inputMinimum",
            "outputMaximum",
            "outputMinimum",
            "uid"});
    for (auto key :
         {"clamp", "gamma", "inputMaximum", "inputMinimum", "outputMaximum", "outputMinimum"})
      required(id, key);
    return id;
  }
  LevelsEffect curve(const spsm::semantic::Action &a,
                     SpsmLevelsPolicy policy,
                     SpsmScalarDomain domain) const
  {
    const auto id = action(a);
    const auto channels = bits(required(a.source, "channelTypes"));
    // The validated ordered lowerer knows this is a scalar mask stack. Keep
    // standalone/bitmap admission strict: an all-channel action is ambiguous
    // outside that context.
    const bool mask_all = ordered_mix_ && domain == SpsmScalarDomain::MaskAuxiliary0 &&
                          channels == UINT64_MAX;
    if (policy != SpsmLevelsPolicy::NativeScalarLevels ||
        (domain != SpsmScalarDomain::Roughness128 && domain != SpsmScalarDomain::MaskAuxiliary0) ||
        (!mask_all && channels != (domain == SpsmScalarDomain::Roughness128 ? 128 : 0)))
      throw SpsmLevelsUnsupported{a.source};
    LevelsEffect e;
    e.channel = 2;
    if (auto enabled = field(a.source, "enabled"))
      e.enabled = std::get<bool>(node(*enabled).value);
    e.input_low = scalar(required(id, "inputMinimum"));
    e.input_high = scalar(required(id, "inputMaximum"));
    e.gamma = scalar(required(id, "gamma"));
    e.output_low = scalar(required(id, "outputMinimum"));
    e.output_high = scalar(required(id, "outputMaximum"));
    const auto clamp = bits(required(id, "clamp"));
    if (clamp > 1 || e.gamma < .01f || e.gamma > 100 || e.input_high - e.input_low < 1e-6f ||
        std::abs(e.input_low) > 65504 || std::abs(e.input_high) > 65504 ||
        std::abs(e.output_low) > 65504 || std::abs(e.output_high) > 65504)
      throw SpsmLevelsUnsupported{id};
    e.clamp = clamp == 1;
    return e;
  }
  LevelsEffect ordered_curve(const spsm::semantic::Action &a,
                             int channel,
                             std::uint64_t selected) const
  {
    if (!ordered_mix_ || !(bits(required(a.source, "channelTypes")) & selected))
      throw SpsmLevelsUnsupported{a.source};
    if (channel == 0)
      return color_curve(a, true);
    const auto id = action(a);
    LevelsEffect e;
    e.channel = channel;
    if (auto flag = field(a.source, "enabled"))
      e.enabled = std::get<bool>(node(*flag).value);
    e.input_low = scalar(required(id, "inputMinimum"));
    e.input_high = scalar(required(id, "inputMaximum"));
    e.gamma = scalar(required(id, "gamma"));
    e.output_low = scalar(required(id, "outputMinimum"));
    e.output_high = scalar(required(id, "outputMaximum"));
    const auto clamp = bits(required(id, "clamp"));
    if (clamp > 1 || e.gamma < .01f || e.gamma > 100 || e.input_high - e.input_low < 1e-6f ||
        std::abs(e.input_low) > 65504 || std::abs(e.input_high) > 65504 ||
        std::abs(e.output_low) > 65504 || std::abs(e.output_high) > 65504)
      throw SpsmLevelsUnsupported{id};
    e.clamp = clamp == 1;
    return e;
  }
  LevelsEffect color_curve(const spsm::semantic::Action &a, bool selected_channel = false) const
  {
    const auto id = action(a);
    if (!selected_channel && bits(required(a.source, "channelTypes")) != 1)
      throw SpsmLevelsUnsupported{a.source};
    const auto low = rgb(required(id, "inputMinimum")), high = rgb(required(id, "inputMaximum")),
               gamma = rgb(required(id, "gamma")), out_low = rgb(required(id, "outputMinimum")),
               out_high = rgb(required(id, "outputMaximum"));
    const auto clamp = bits(required(id, "clamp"));
    if (clamp > 1)
      throw SpsmLevelsUnsupported{id};
    LevelsEffect e;
    e.channel = 0;
    e.clamp = clamp == 1;
    if (auto enabled = field(a.source, "enabled"))
      e.enabled = std::get<bool>(node(*enabled).value);
    // Master stays identity: native per-component curves run in the explicitly
    // chosen source-linear space before the separate named matrix boundary.
    for (int c = 0; c < 3; ++c) {
      if (gamma[c] < .01f || gamma[c] > 100 || high[c] - low[c] < 1e-6f ||
          std::abs(low[c]) > 65504 || std::abs(high[c]) > 65504 || std::abs(out_low[c]) > 65504 ||
          std::abs(out_high[c]) > 65504)
        throw SpsmLevelsUnsupported{id};
      auto &curve = e.rgb_curves[c];
      curve.input_low = low[c];
      curve.input_high = high[c];
      curve.gamma = gamma[c];
      curve.output_low = out_low[c];
      curve.output_high = out_high[c];
    }
    return e;
  }
};
}  // namespace spsm_levels_detail
// Validates even disjoint Color Levels structurally, without interpreting RGB.
inline spsm::NodeId validate_spsm_levels_action(const spsm::Preset &p,
                                                const spsm::semantic::Action &a)
{
  return spsm_levels_detail::Parser(p).action(a);
}
inline LevelsEffect parse_spsm_scalar_levels(
    const spsm::Preset &p,
    const spsm::semantic::Action &a,
    SpsmLevelsPolicy policy,
    SpsmScalarDomain domain = SpsmScalarDomain::Roughness128,
    bool validated_ordered_mix = false)
{
  return spsm_levels_detail::Parser(p, validated_ordered_mix).curve(a, policy, domain);
}
// This function is only used after explicit native RGB interpretation opt-in;
// its equations/reciprocal gamma are not a source appearance equivalence claim.
inline LevelsEffect parse_spsm_native_rgb_levels(const spsm::Preset &p,
                                                 const spsm::semantic::Action &a,
                                                 bool validated_ordered_mix = false)
{
  return spsm_levels_detail::Parser(p, validated_ordered_mix).color_curve(a);
}
}  // namespace bts
