// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeWorldNormalPlan.hh"
#include "PaintRasterSource.hh"
#include <QJsonObject>
#include <limits>

namespace bts::native_world_normal_plan_test {
inline void require(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
struct NoPixels final : PaintRasterSource {
  mutable int evaluations = 0;
  PaintPixels evaluate_cpu() const override
  {
    ++evaluations;
    throw std::runtime_error("World Normal metadata sampled Paint");
  }
};
inline TextureLayer native()
{
  TextureLayer layer;
  layer.kind = LayerKind::Fill;
  auto description = std::make_shared<NativeMaterialDescription>();
  description->material = "Owned native fixture";
  layer.fill.native_material = std::move(description);
  layer.fill.material_resource = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  return layer;
}
inline TextureSet fixture()
{
  TextureSet set;
  set.layers = {native()};
  set.selected_layer = set.layers.front().id;
  return set;
}
}  // namespace bts::native_world_normal_plan_test
namespace bts {
// Registered by the native CPU owner; explicit throws remain active in Release.
inline QJsonObject verify_native_world_normal_plan()
{
  using namespace native_world_normal_plan_test;
  const auto materialized = PaintPixels::recipe_materializations(),
             composed = local_paint_materializations, hashed = PaintPixels::hash_bytes();
  GpuPersistenceScope forbid_cpu;
  int cases = 0;
  const auto check = [&](const TextureSet &set, bool eligible, const char *reason = nullptr) {
    const std::array ids{set.selected_layer};
    const auto plan = native_world_normal_plan(set, ids);
    ++cases;
    require(plan.eligible == eligible, "World Normal eligibility differs");
    if (reason)
      require(plan.layer == ids[0] && !plan.resource.empty() &&
                  plan.reason.contains(QString::fromUtf8(reason)),
              "World Normal rejection lost candidate/reason");
    const auto source = std::find_if(set.layers.begin(), set.layers.end(), [&](const auto &layer) {
      return layer.id == ids[0];
    });
    if (eligible)
      require(source != set.layers.end() && plan.layer == ids[0] &&
                  plan.resource == source->fill.material_resource && plan.reason.isEmpty(),
              "World Normal accepted identity/reason differs");
    return plan;
  };
  auto set = fixture();
  check(set, true);
  set.channels[3] = false;
  set.displacement.channel = true;
  set.displacement.enabled = true;
  check(set, true);
  for (int mode = 0; mode <= 6; ++mode) {
    set.layers.front().fill.mode = mode;
    set.layers.front().fill.scale = {2, .5};
    set.layers.front().fill.offset = {.25, -.125};
    set.layers.front().fill.rotation = 37;
    set.layers.front().fill.physical = true;
    check(set, true);
  }
  set = fixture();
  const auto no_candidate = native_world_normal_plan(set, {});
  ++cases;
  require(no_candidate.layer.isNull() && no_candidate.resource.empty() &&
              no_candidate.reason.isEmpty() && !no_candidate.eligible,
          "Flat native source became a candidate");
  // Multiple native normal sources coexist with scalar, painted and baked detail.
  set.layers.push_back(native());
  {
    const std::array ids{set.layers[0].id, set.layers[1].id};
    const auto plan = native_world_normal_plan(set, ids);
    ++cases;
    require(plan.eligible && plan.contributions.size() == 2,
            "Multiple normal sources were rejected");
  }
  set.layers.back().values.bump = .7f;
  check(set, true);
  TextureLayer scalar;
  scalar.kind = LayerKind::Fill;
  scalar.values.bump = .75f;
  set.layers.push_back(scalar);
  check(set, true);
  set.base_values.bump = .6f;
  set.base_values.normal = {0, .1f, 1};
  check(set, true);
  set = fixture();
  set.layers.front().blending[4] = LayerBlend::Multiply;
  check(set, true);
  auto raw = std::make_shared<NativeMaterialDescription>(*set.layers.front().fill.native_material);
  raw->height_policy = NativeHeightPolicy::RawAuthoredFloatV1;
  set.layers.front().fill.native_material = raw;
  check(set, false);
  set = fixture();
  TextureLayer folder, root;
  folder.kind = root.kind = LayerKind::Folder;
  folder.parent = root.id;
  set.layers.front().parent = folder.id;
  set.layers.front().mask = LayerMask{};
  folder.mask = LayerMask{};
  folder.opacity[4] = .37f;
  root.opacity[4] = .6f;
  set.layers.push_back(folder);
  set.layers.push_back(root);
  auto plan = check(set, true);
  require(plan.ancestors == std::vector<QUuid>{folder.id, root.id},
          "Normal ancestor order differs");
  set.layers[1].visible = false;
  check(set, false);
  set.layers[1].visible = true;
  set.layers[2].opacity[4] = 0;
  check(set, false);
  set.layers[2].opacity[4] = .6f;
  set.layers[1].channels[4] = false;
  check(set, false);
  set.layers[1].channels[4] = true;
  set.layers[1].blending[4] = LayerBlend::Passthrough;
  check(set, true);
  MaskEffect compare;
  compare.kind = MaskEffectKind::Compare;
  compare.left = 0;
  set.layers[1].mask->effects.push_back(compare);
  check(set, true);
  {
    auto expanded = fixture();
    expanded.layers.back().instance = LayerInstance{};
    auto copy = native();
    expanded.instance_layers = std::make_shared<const std::vector<TextureLayer>>(
        std::vector<TextureLayer>{copy});
    const std::array ids{copy.id};
    const auto accepted = native_world_normal_plan(expanded, ids);
    ++cases;
    require(accepted.eligible && accepted.layer == copy.id,
            "Resolved native layer instance rejected");
  }
  auto no_pixels = std::make_shared<NoPixels>();
  PaintPixels::PageVersions versions;
  versions[4][0] = 1;
  auto pixels = PaintPixels::from_source(128, no_pixels, versions);
  set = fixture();
  TextureLayer painted;
  painted.pixels.assign_tile(pixels, 1002);
  set.layers.push_back(painted);
  check(set, true);
  LevelsEffect effect;
  effect.channel = 4;
  set.layers.front().effects.push_back(effect);
  check(set, true);
  const auto rejects = [&](TextureSet invalid, std::vector<QUuid> ids) {
    bool rejected = false;
    try {
      native_world_normal_plan(invalid, ids);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    ++cases;
    require(rejected, "Malformed world Normal metadata did not throw");
  };
  set = fixture();
  const auto id = set.layers[0].id;
  rejects(set, {QUuid::createUuid()});
  rejects(set, {id, id});
  auto invalid = set;
  invalid.layers[0].id = QUuid();
  rejects(invalid, {});
  invalid = set;
  invalid.layers.push_back(invalid.layers[0]);
  rejects(invalid, {id});
  invalid = set;
  invalid.layers[0].parent = QUuid::createUuid();
  rejects(invalid, {id});
  invalid = set;
  invalid.layers[0].opacity[4] = std::numeric_limits<float>::quiet_NaN();
  rejects(invalid, {id});
  invalid = set;
  invalid.layers[0].fill.native_material.reset();
  rejects(invalid, {id});
  require(no_pixels->evaluations == 0, "World Normal planner evaluated a Paint recipe");
  require(PaintPixels::recipe_materializations() == materialized &&
              local_paint_materializations == composed && PaintPixels::hash_bytes() == hashed,
          "World Normal planner changed CPU materialization/hash counters");
  return {{"complete", true},
          {"cases", cases},
          {"recipe_evaluations", 0},
          {"cpu_materializations", 0},
          {"cpu_layer_compositions", 0},
          {"paint_hash_bytes", 0},
          {"pixel_reads", false}};
}
}  // namespace bts
